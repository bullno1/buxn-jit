#ifndef BUXN_JIT_H
#define BUXN_JIT_H

#include <stdint.h>
#include <stddef.h>

struct buxn_vm_s;

typedef struct buxn_jit_s buxn_jit_t;
typedef struct buxn_jit_hook_ctx_s buxn_jit_hook_ctx_t;
typedef struct buxn_jit_addr_mark_s buxn_jit_addr_mark_t;

typedef struct {
	size_t code_size;
	int num_blocks;
	int num_bounces;
} buxn_jit_stats_t;

typedef struct buxn_jit_hook_s {
	void* userdata;

	void (*begin_block)(void* userdata, buxn_jit_hook_ctx_t* ctx);
	void (*jit_opcode)(void* userdata, buxn_jit_hook_ctx_t* ctx, uint16_t pc, uint8_t opcode);
	void (*end_block)(void* userdata, buxn_jit_hook_ctx_t* ctx, uintptr_t start, size_t size);
} buxn_jit_hook_t;

typedef struct {
	void* mem_ctx;
	buxn_jit_hook_t* hook;
} buxn_jit_config_t;

buxn_jit_t*
buxn_jit_init(struct buxn_vm_s* vm, const buxn_jit_config_t* config);

buxn_jit_stats_t*
buxn_jit_stats(buxn_jit_t* jit);

void
buxn_jit_execute(buxn_jit_t* jit, uint16_t pc);

void
buxn_jit_cleanup(buxn_jit_t* jit);

// Hook API

uint16_t
buxn_jit_hook_get_entry_addr(buxn_jit_hook_ctx_t* ctx);

buxn_jit_addr_mark_t*
buxn_jit_hook_mark_addr(buxn_jit_hook_ctx_t* ctx);

uintptr_t
buxn_jit_hook_resolve_addr(buxn_jit_hook_ctx_t* ctx, buxn_jit_addr_mark_t* mark);

// Must be provided by the host program

extern void*
buxn_jit_alloc(void* mem_ctx, size_t size, size_t alignment);

#endif
