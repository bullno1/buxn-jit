#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <barena.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>
#include <buxn/devices/console.h>
#include <buxn/devices/system.h>
#include <buxn/devices/datetime.h>

typedef struct {
	buxn_console_t console;
	buxn_jit_t* jit;
} vm_data_t;

static inline void
buxn_console_send_data_jit(
	buxn_vm_t* vm,
	buxn_console_t* device,
	uint8_t type,
	uint8_t value
) {
	device->type = type;
	device->value = value;

	uint16_t vector_addr = buxn_vm_dev_load2(vm, 0x10);
	vm_data_t* vm_data = vm->config.userdata;
	buxn_jit_execute(vm_data->jit, vector_addr);
}

static void
buxn_console_send_input_jit(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	buxn_console_send_data_jit(vm, device, BUXN_CONSOLE_STDIN, c);
}

static void
buxn_console_send_input_end_jit(struct buxn_vm_s* vm, buxn_console_t* device) {
	buxn_console_send_data_jit(vm, device, BUXN_CONSOLE_END, 0);
}

static void
buxn_console_send_args_jit(struct buxn_vm_s* vm, buxn_console_t* device) {
	while (device->argc > 0) {
		const char* ch = &device->argv[0][0];

		while (1) {
			char arg_ch = *ch;

			if (arg_ch != 0) {
				buxn_console_send_data_jit(vm, device, BUXN_CONSOLE_ARG, arg_ch);
			} else if (device->argc == 1) {
				buxn_console_send_data_jit(vm, device, BUXN_CONSOLE_END, '\n');
			} else {
				buxn_console_send_data_jit(vm, device, BUXN_CONSOLE_ARG_SEP, '\n');
			}

			if (arg_ch != 0) {
				++ch;
			} else {
				--device->argc;
				++device->argv;
				break;
			}
		}
	}
}

static int
boot(int argc, const char* argv[], FILE* rom_file, uint32_t rom_size) {
	int exit_code = 0;
	vm_data_t devices = { 0 };

	buxn_vm_t* vm = malloc(sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS);
	vm->config = (buxn_vm_config_t){
		.userdata = &devices,
		.memory_size = BUXN_MEMORY_BANK_SIZE * BUXN_MAX_NUM_MEMORY_BANKS,
	};
	buxn_vm_reset(vm, BUXN_VM_RESET_ALL);

	barena_pool_t pool;
	barena_pool_init(&pool, 1);
	barena_t arena;
	barena_init(&arena, &pool);
	buxn_jit_t* jit = buxn_jit_init(vm, &(buxn_jit_config_t){
		.mem_ctx = &arena,
	});
	buxn_jit_stats_t* stats = buxn_jit_stats(jit);
	devices.jit = jit;

	// Read rom
	{
		uint8_t* read_pos = &vm->memory[BUXN_RESET_VECTOR];
		if (rom_size == 0) {
			while (read_pos < vm->memory + vm->config.memory_size) {
				size_t num_bytes = fread(read_pos, 1, 1024, rom_file);
				if (num_bytes == 0) { break; }
				read_pos += num_bytes;
			}
		} else {
			fread(read_pos, rom_size, 1, rom_file);
		}
	}
	fclose(rom_file);

	buxn_console_init(vm, &devices.console, argc, argv);

	buxn_jit_execute(jit, BUXN_RESET_VECTOR);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	buxn_console_send_args_jit(vm, &devices.console);
	if ((exit_code = buxn_system_exit_code(vm)) > 0) {
		goto end;
	}

	while (
		buxn_system_exit_code(vm) < 0
		&& buxn_console_should_send_input(vm)
	) {
		int ch = fgetc(stdin);
		if (ch != EOF) {
			buxn_console_send_input_jit(vm, &devices.console, ch);
		} else {
			buxn_console_send_input_end_jit(vm, &devices.console);
			break;
		}
	}

	exit_code = buxn_system_exit_code(vm);
	if (exit_code < 0) { exit_code = 0; }
end:
	fprintf(stderr, "Num blocks: %d\n", stats->num_blocks);
	fprintf(stderr, "Num bounces: %d\n", stats->num_bounces);

	buxn_jit_cleanup(jit);
	barena_reset(&arena);
	barena_pool_cleanup(&pool);

	free(vm);
	return exit_code;
}

int
main(int argc, const char* argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: buxn-cli <rom>\n");
		return 1;
	}
	int exit_code = 0;

	FILE* rom_file;
	if ((rom_file = fopen(argv[1], "rb")) == NULL) {
		perror("Error while opening rom file");
		exit_code = 1;
		goto end;
	}

	exit_code = boot(argc - 2, argv + 2, rom_file, 0);

end:
	return exit_code;
}


uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	vm_data_t* devices = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &devices->console, address);
		case BUXN_DEVICE_DATETIME:
			return buxn_datetime_dei(vm, address);
		default:
			return vm->device[address];
	}
}

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	vm_data_t* devices = vm->config.userdata;
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &devices->console, address);
			break;
	}
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
	if (value == 0) { return; }

	fprintf(stderr, "WST");
	for (uint8_t i = 0; i < vm->wsp; ++i) {
		fprintf(stderr, " %02hhX", vm->ws[i]);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "RST");
	for (uint8_t i = 0; i < vm->rsp; ++i) {
		fprintf(stderr, " %02hhX", vm->rs[i]);
	}
	fprintf(stderr, "\n");
}

void
buxn_system_set_metadata(buxn_vm_t* vm, uint16_t address) {
	(void)vm;
	(void)address;
}

void
buxn_system_theme_changed(buxn_vm_t* vm) {
	(void)vm;
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stdout);
	fflush(stdout);
}

void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stderr);
	fflush(stdout);
}

void*
buxn_jit_alloc(void* ctx, size_t size, size_t alignment) {
	return barena_memalign(ctx, size, alignment);
}

#define BLIB_IMPLEMENTATION
#include <barena.h>
