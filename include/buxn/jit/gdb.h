#ifndef BUXN_JIT_GDB_H
#define BUXN_JIT_GDB_H

#include "label_map.h"

struct buxn_jit_dbg_hook_s;

typedef struct {
	void* mem_ctx;
	const buxn_label_map_t* label_map;
} buxn_jit_gdb_hook_config_t;

void
buxn_jit_init_gdb_hook(
	struct buxn_jit_dbg_hook_s* hook,
	const buxn_jit_gdb_hook_config_t* config
);

void
buxn_jit_cleanup_gdb_hook(
	struct buxn_jit_dbg_hook_s* hook
);

#endif
