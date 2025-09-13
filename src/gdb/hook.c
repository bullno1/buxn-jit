#include <buxn/vm/jit.h>
#include <buxn/dbg/jit-gdb.h>
#include <stdio.h>
#include "dbg_info.h"

// gdb API {{{

typedef enum {
	JIT_NOACTION = 0,
	JIT_REGISTER_FN,
	JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
	struct jit_code_entry *next_entry;
	struct jit_code_entry *prev_entry;
	const char *symfile_addr;
	uint64_t symfile_size;
};

struct jit_descriptor {
	uint32_t version;
	/* This type should be jit_actions_t, but we use uint32_t
	   to be explicit about the bitwidth.  */
	uint32_t action_flag;
	struct jit_code_entry *relevant_entry;
	struct jit_code_entry *first_entry;
};

__attribute__((used,visibility("default"),noinline))
void
__jit_debug_register_code(void) {
    __asm__ __volatile__ ("");
}

__attribute__((used))
struct jit_descriptor __jit_debug_descriptor = {
	.version = 1,
	.action_flag = JIT_NOACTION,
	.first_entry = NULL,
	.relevant_entry = NULL,
};

// }}}

static buxn_jit_gdb_hook_config_t buxn_jit_gdb_default_config = { 0 };

static void
buxn_jit_gdb_register(
	void* userdata,
	uint16_t addr,
	uintptr_t start, size_t size
) {
	buxn_jit_gdb_hook_config_t* config = userdata;

	buxn_jit_dbg_info_t* dbg_info = buxn_jit_alloc(
		config->mem_ctx,
		sizeof(buxn_jit_dbg_info_t),
		_Alignof(buxn_jit_dbg_info_t)
	);
	*dbg_info = (buxn_jit_dbg_info_t){
		.addr = addr,
		.start = start,
		.size = size,
	};

	struct jit_code_entry* entry = buxn_jit_alloc(
		config->mem_ctx,
		sizeof(struct jit_code_entry),
		_Alignof(struct jit_code_entry)
	);
	*entry = (struct jit_code_entry){
		.symfile_addr = (const char*)dbg_info,
		.symfile_size = sizeof(*dbg_info),
	};
	entry->next_entry = __jit_debug_descriptor.first_entry;
	if (entry->next_entry) {
		entry->next_entry->prev_entry = entry;
	}
	__jit_debug_descriptor.first_entry = entry;

	__jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
	__jit_debug_descriptor.relevant_entry = entry;
	__jit_debug_register_code();
}

void
buxn_jit_init_gdb_hook(
	struct buxn_jit_dbg_hook_s* hook,
	buxn_jit_gdb_hook_config_t* config
) {

	*hook = (buxn_jit_dbg_hook_t){
		.register_block = buxn_jit_gdb_register,
		.userdata = config != NULL ? config : &buxn_jit_gdb_default_config,
	};
}
