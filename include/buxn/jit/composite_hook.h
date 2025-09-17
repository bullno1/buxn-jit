#ifndef BUXN_JIT_COMPOSITE_HOOK
#define BUXN_JIT_COMPOSITE_HOOK

#include <buxn/jit.h>

static inline void
buxn_jit_composite_hook_begin_block(void* userdata, buxn_jit_hook_ctx_t* ctx) {
	buxn_jit_hook_t** hooks = userdata;
	for (buxn_jit_hook_t** hook = hooks; *hook != NULL; ++hook) {
		if ((*hook)->begin_block) {
			(*hook)->begin_block((*hook)->userdata, ctx);
		}
	}
}

static inline void
buxn_jit_composite_hook_jit_opcode(void* userdata, buxn_jit_hook_ctx_t* ctx, uint16_t pc, uint8_t opcode) {
	buxn_jit_hook_t** hooks = userdata;
	for (buxn_jit_hook_t** hook = hooks; *hook != NULL; ++hook) {
		if ((*hook)->jit_opcode) {
			(*hook)->jit_opcode((*hook)->userdata, ctx, pc, opcode);
		}
	}
}

static inline void
buxn_jit_composite_hook_end_block(void* userdata, buxn_jit_hook_ctx_t* ctx, uintptr_t start, size_t size) {
	buxn_jit_hook_t** hooks = userdata;
	for (buxn_jit_hook_t** hook = hooks; *hook != NULL; ++hook) {
		if ((*hook)->end_block) {
			(*hook)->end_block((*hook)->userdata, ctx, start, size);
		}
	}
}

static inline void
buxn_jit_init_composite_hook(buxn_jit_hook_t* composite_hook, buxn_jit_hook_t* hooks[]) {
	*composite_hook = (buxn_jit_hook_t){
		.userdata = hooks,
		.begin_block = buxn_jit_composite_hook_begin_block,
		.jit_opcode = buxn_jit_composite_hook_jit_opcode,
		.end_block = buxn_jit_composite_hook_end_block,
	};
}

#endif
