#ifndef BUXN_JIT_H
#define BUXN_JIT_H

#include <stdint.h>
#include <stddef.h>

struct buxn_vm_s;

typedef struct buxn_jit_s buxn_jit_t;
typedef struct buxn_jit_alloc_ctx_s buxn_jit_alloc_ctx_t;

buxn_jit_t*
buxn_jit_init(struct buxn_vm_s* vm, buxn_jit_alloc_ctx_t* alloc_ctx);

void
buxn_jit_execute(buxn_jit_t* jit, uint16_t pc);

void
buxn_jit_cleanup(buxn_jit_t* jit);

// Must be provided by the host program

extern void*
buxn_jit_alloc(buxn_jit_alloc_ctx_t*, size_t size, size_t alignment);

#endif
