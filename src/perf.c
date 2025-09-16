#define _GNU_SOURCE
#include <buxn/jit/perf.h>
#include <buxn/jit.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <elf.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#if defined(__i386__) || defined(__i386)
#	define PERF_EM EM_386
#elif defined(__x86_64__)
#	define PERF_EM EM_X86_64
#elif defined(__arm__) || defined (__ARM__)
#	define PERF_EM EM_ARM
#elif defined(__aarch64__)
#	define PERF_EM EM_AARCH64
#else
#	warning "Unsupported architecture"
#endif

typedef struct {
	FILE* map_file;
	FILE* dump_file;
	uint32_t pid;
	uint32_t code_index;
	buxn_jit_perf_hook_config_t config;
} buxn_jit_perf_hook_data_t;

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t total_size;
	uint32_t elf_mach;
	uint32_t pad1;
	uint32_t pid;
	uint64_t timestamp;
	uint64_t flags;
} perf_jitdump_hdr_t;

typedef enum {
	JIT_CODE_LOAD = 0,
	JIT_CODE_MOVE,
	JIT_CODE_DEBUG_INFO,
	JIT_CODE_CLOSE,
	JIT_CODE_UNWINDING_INFO,
} perf_jitdump_rec_id_t;

typedef struct {
	uint32_t id;
	uint32_t total_size;
	uint64_t timestamp;
} perf_jitdump_rec_hdr_t;

typedef struct {
	uint32_t pid;
	uint32_t tid;
	uint64_t vma;
	uint64_t code_addr;
	uint64_t code_size;
	uint64_t code_index;
} perf_jitdump_code_load_t;

static uint64_t
perf_jitdump_timestamp(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0; // error handling if needed
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void
buxn_jit_perf_register_block(
	void* userdata,
	uint16_t addr,
	uintptr_t code_start, size_t code_size
) {
	buxn_jit_perf_hook_data_t* hook_data = userdata;

	const char* at_str = "@";
	const char* label = "?";
	int label_len = 1;
	if (hook_data->config.label_map != NULL) {
		const buxn_label_map_entry_t* closest_label = buxn_pc_to_label(
			hook_data->config.label_map, addr
		);
		label = closest_label->name;
		label_len = closest_label->name_len;
		at_str = closest_label->addr == addr ? "@" : "~";
	}
	char name[512];
	int name_len = snprintf(
		name, sizeof(name),
		"uxn:0x%04x%s%.*s",
		addr, at_str, label_len, label
	);

	if (hook_data->map_file != NULL) {
		fprintf(
			hook_data->map_file,
			"%" PRIxPTR " %zx %.*s\n",
			code_start, code_size, name_len, name
		);
		fflush(hook_data->map_file);
	}

	if (hook_data->dump_file != NULL) {
		perf_jitdump_code_load_t code_load = {
			.pid = hook_data->pid,
			.tid = gettid(),
			.vma = code_start,
			.code_addr = code_start,
			.code_size = code_size,
			.code_index = hook_data->code_index++,
		};
		perf_jitdump_rec_hdr_t hdr = {
			.id = JIT_CODE_LOAD,
			.timestamp = perf_jitdump_timestamp(),
			.total_size = 0
				+ sizeof(hdr)
				+ sizeof(code_load)
				+ (size_t)(name_len + 1)
				+ code_size
		};

		fwrite(&hdr, sizeof(hdr), 1, hook_data->dump_file);
		fwrite(&code_load, sizeof(code_load), 1, hook_data->dump_file);
		fwrite(name, name_len + 1, 1, hook_data->dump_file);
		fwrite((void*)code_start, code_size, 1, hook_data->dump_file);
		fflush(hook_data->dump_file);
	}
}

void
buxn_jit_init_perf_hook(
	struct buxn_jit_dbg_hook_s* hook,
	const buxn_jit_perf_hook_config_t* config
) {
	int pid = getpid();

	char map_file_path[512];
	snprintf(map_file_path, sizeof(map_file_path), "/tmp/perf-%d.map", pid);

	// The filename must have this pattern
	// https://github.com/torvalds/linux/blob/46a51f4f5edade43ba66b3c151f0e25ec8b69cb6/tools/perf/util/jitdump.c#L749-L753
	char dump_file_path[512];
	snprintf(dump_file_path, sizeof(dump_file_path), "/tmp/jit-%d.dump", pid);

	buxn_jit_perf_hook_data_t* hook_data = buxn_jit_alloc(
		config->mem_ctx,
		sizeof(buxn_jit_perf_hook_data_t),
		_Alignof(buxn_jit_perf_hook_data_t)
	);
	*hook_data = (buxn_jit_perf_hook_data_t){
		.config = *config,
		.map_file = fopen(map_file_path, "wb"),
		.dump_file = fopen(dump_file_path, "wb"),
		.pid = pid,
	};

	if (hook_data->dump_file != NULL) {
		// Write header
		perf_jitdump_hdr_t hdr = {
			.magic = 0x4A695444,
			.version = 1,
			.total_size = sizeof(perf_jitdump_hdr_t),
			.elf_mach = PERF_EM,
			.pid = pid,
			.timestamp = perf_jitdump_timestamp(),
			.flags = 0,
		};
		fwrite(&hdr, sizeof(hdr), 1, hook_data->dump_file);
		fflush(hook_data->dump_file);
	}

	hook->userdata = hook_data;
	hook->register_block = buxn_jit_perf_register_block;

	// Apparently, the dump file only has to be mapped once to inform perf
	// https://theunixzoo.co.uk/blog/2025-09-14-linux-perf-jit.html#fn:marker
	int fd = open(dump_file_path, O_RDONLY);
	ssize_t size = lseek(fd, 0, SEEK_END);
	void* map = mmap(NULL, size, PROT_READ|PROT_EXEC, MAP_PRIVATE, fd, 0);
	munmap(map, size);
	close(fd);
}

void
buxn_jit_cleanup_perf_hook(
	struct buxn_jit_dbg_hook_s* hook
) {
	buxn_jit_perf_hook_data_t* hook_data = hook->userdata;
	if (hook_data->map_file != NULL) {
		fclose(hook_data->map_file);
	}
	if (hook_data->dump_file != NULL) {
		fclose(hook_data->dump_file);
	}
}
