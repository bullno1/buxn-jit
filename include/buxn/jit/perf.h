#ifndef BUXN_JIT_PERF_H
#define BUXN_JIT_PERF_H

// Create support files for perf using:
// * https://github.com/torvalds/linux/blob/4da6552c5d07bfc88576ed9ad7fc81fce4c3ba41/tools/perf/Documentation/jit-interface.txt
// * https://github.com/torvalds/linux/blob/4da6552c5d07bfc88576ed9ad7fc81fce4c3ba41/tools/perf/Documentation/jitdump-specification.txt

#include "label_map.h"

struct buxn_jit_hook_s;

typedef struct {
	void* mem_ctx;
	const buxn_label_map_t* label_map;
} buxn_jit_perf_hook_config_t;

void
buxn_jit_init_perf_hook(
	struct buxn_jit_hook_s* hook,
	const buxn_jit_perf_hook_config_t* config
);

void
buxn_jit_cleanup_perf_hook(
	struct buxn_jit_hook_s* hook
);

#endif
