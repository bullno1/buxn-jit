#include <buxn/jit/perf.h>
#include <buxn/jit.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>

typedef struct {
	FILE* map_file;
	buxn_jit_perf_hook_config_t config;
} buxn_jit_perf_hook_data_t;

static inline void
buxn_jit_perf_register_block(void* userdata, uint16_t addr, uintptr_t start, size_t size) {
	buxn_jit_perf_hook_data_t* hook_data = userdata;
	if (hook_data->map_file != NULL) {
		const char* at_str = "@";
		const char* label = "?";
		int label_len = 1;
		if (hook_data->config.label_map != NULL) {
			const buxn_label_map_entry_t* closest_label = buxn_pc_to_label(hook_data->config.label_map, addr);
			label = closest_label->name;
			label_len = closest_label->name_len;
			at_str = closest_label->addr == addr ? "@" : "~";
		}

		fprintf(
			hook_data->map_file,
			"%" PRIxPTR " %zx uxn:0x%04x%s%.*s\n",
			start, size, addr, at_str, label_len, label
		);
		fflush(hook_data->map_file);
	}
}

void
buxn_jit_init_perf_hook(
	struct buxn_jit_dbg_hook_s* hook,
	const buxn_jit_perf_hook_config_t* config
) {
	char map_file_path[512];
	snprintf(map_file_path, sizeof(map_file_path), "/tmp/perf-%d.map", getpid());

	buxn_jit_perf_hook_data_t* hook_data = buxn_jit_alloc(
		config->mem_ctx,
		sizeof(buxn_jit_perf_hook_data_t),
		_Alignof(buxn_jit_perf_hook_data_t)
	);
	*hook_data = (buxn_jit_perf_hook_data_t){
		.config = *config,
		.map_file = fopen(map_file_path, "wb"),
	};

	hook->userdata = hook_data;
	hook->register_block = buxn_jit_perf_register_block;
}

void
buxn_jit_cleanup_perf_hook(
	struct buxn_jit_dbg_hook_s* hook
) {
	buxn_jit_perf_hook_data_t* hook_data = hook->userdata;
	if (hook_data->map_file != NULL) {
		fclose(hook_data->map_file);
	}
}
