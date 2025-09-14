#ifndef BUXN_JIT_H
#define BUXN_JIT_H

#include <stdint.h>
#include <stddef.h>

struct buxn_vm_s;

typedef struct buxn_jit_s buxn_jit_t;

typedef struct {
	int num_blocks;
	int num_bounces;
} buxn_jit_stats_t;

typedef struct buxn_jit_dbg_hook_s {
	void* userdata;
	void (*register_block)(void* userdata, uint16_t addr, uintptr_t start, size_t size);
} buxn_jit_dbg_hook_t;

typedef struct {
	void* mem_ctx;
	buxn_jit_dbg_hook_t* dbg_hook;
} buxn_jit_config_t;

buxn_jit_t*
buxn_jit_init(struct buxn_vm_s* vm, buxn_jit_config_t* config);

buxn_jit_stats_t*
buxn_jit_stats(buxn_jit_t* jit);

void
buxn_jit_execute(buxn_jit_t* jit, uint16_t pc);

void
buxn_jit_cleanup(buxn_jit_t* jit);

// Must be provided by the host program

extern void*
buxn_jit_alloc(void* mem_ctx, size_t size, size_t alignment);

#endif
