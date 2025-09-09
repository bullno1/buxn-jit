#include <btest.h>
#include <barena.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>
#include <buxn/devices/system.h>
#include <buxn/devices/console.h>
#include "common.h"
#include "resources.rc"

static struct {
	barena_pool_t pool;
	barena_t arena;
	buxn_jit_t* jit;
	buxn_vm_t* vm;

	device_handler_t device_handler;
	buxn_console_t console;
} fixture;

static uint8_t
dei(buxn_vm_t* vm, uint8_t address, void* userdata) {
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			return buxn_system_dei(vm, address);
		case BUXN_DEVICE_CONSOLE:
			return buxn_console_dei(vm, &fixture.console, address);
		default:
			return vm->device[address];
	}
}

static void
deo(buxn_vm_t* vm, uint8_t address, void* userdata) {
	uint8_t device_id = buxn_device_id(address);
	switch (device_id) {
		case BUXN_DEVICE_SYSTEM:
			buxn_system_deo(vm, address);
			break;
		case BUXN_DEVICE_CONSOLE:
			buxn_console_deo(vm, &fixture.console, address);
			break;
	}
}

static void
init_per_suite(void) {
	barena_pool_init(&fixture.pool, 1);
}

static void
cleanup_per_suite(void) {
	barena_pool_cleanup(&fixture.pool);
}

static void
init_per_test(void) {
	barena_init(&fixture.arena, &fixture.pool);
	fixture.vm = barena_memalign(
		&fixture.arena,
		sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE,
		_Alignof(buxn_vm_t)
	);
	fixture.vm->config = (buxn_vm_config_t){
		.memory_size = BUXN_MEMORY_BANK_SIZE,
		.userdata = &fixture.device_handler,
	};
	fixture.device_handler = (device_handler_t){
		.dei = dei,
		.deo = deo,
	};
	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);

	fixture.jit = buxn_jit_init(fixture.vm, (buxn_jit_alloc_ctx_t*)&fixture.arena);
}

static void
cleanup_per_test(void) {
	buxn_jit_cleanup(fixture.jit);
	barena_reset(&fixture.arena);
}

static btest_suite_t opctest = {
	.name = "opctest",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(opctest, test) {
	xincbin_data_t opctest = XINCBIN_GET(opctest_tal);
	BTEST_ASSERT(buxn_asm_str_len(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		(char*)opctest.data, opctest.size, "opctest.tal", 0
	));

	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);
	BTEST_EXPECT(buxn_system_exit_code(fixture.vm) <= 0);
}
