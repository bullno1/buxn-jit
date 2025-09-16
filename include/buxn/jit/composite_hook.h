#ifndef BUXN_JIT_COMPOSITE_HOOK
#define BUXN_JIT_COMPOSITE_HOOK

#include <buxn/jit.h>

static inline void
buxn_jit_composite_hook_register_block(void* userdata, uint16_t addr, uintptr_t start, size_t size) {
	buxn_jit_dbg_hook_t** hooks = userdata;
	for (buxn_jit_dbg_hook_t** hook = hooks; *hook != NULL; ++hook) {
		(*hook)->register_block((*hook)->userdata, addr, start, size);
	}
}

static inline void
buxn_jit_init_composite_hook(buxn_jit_dbg_hook_t* composite_hook, buxn_jit_dbg_hook_t* hooks[]) {
	composite_hook->userdata = hooks;
	composite_hook->register_block = buxn_jit_composite_hook_register_block;
}

#endif
