#include <buxn/dbg/jit-gdb.h>
#include <buxn/vm/jit.h>
#include <stdbool.h>
#include <threads.h>
#include <string.h>
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
	// Prevent the optimizer from ever removing calls to this function
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

typedef struct buxn_jit_gdb_info_block_s buxn_jit_gdb_info_block_t;
struct buxn_jit_gdb_info_block_s {
	buxn_jit_gdb_info_block_t* next;

	struct jit_code_entry entry;
	buxn_jit_dbg_info_t debug_info;
};

typedef struct {
	buxn_jit_gdb_info_block_t* blocks;
	buxn_jit_gdb_hook_config_t config;
} buxn_jit_dbg_hook_data_t;

static once_flag buxn_jit_gdb_once = ONCE_FLAG_INIT;
static mtx_t buxn_jit_gdb_hook_mtx;

static void
buxn_jit_gdb_init(void) {
	mtx_init(&buxn_jit_gdb_hook_mtx, mtx_plain);
}

static void
buxn_jit_gdb_register(
	void* userdata,
	uint16_t addr,
	uintptr_t start, size_t size
) {
	buxn_jit_dbg_hook_data_t* hook_data = userdata;

	buxn_jit_gdb_info_block_t* block = buxn_jit_alloc(
		hook_data->config.mem_ctx,
		sizeof(buxn_jit_gdb_info_block_t),
		_Alignof(buxn_jit_gdb_info_block_t)
	);
	*block = (buxn_jit_gdb_info_block_t){
		.debug_info = {
			.addr = addr,
			.start = start,
			.size = size,
		},
		.entry = {
			.symfile_addr = (const char*)&block->debug_info,
			.symfile_size = sizeof(block->debug_info),
		},
	};

	const buxn_label_map_entry_t* label_map = NULL;
	if (hook_data->config.label_map != NULL) {
		label_map = buxn_pc_to_label(hook_data->config.label_map, addr);
	}
	if (label_map != NULL) {
		char* name = buxn_jit_alloc(
			hook_data->config.mem_ctx,
			label_map->name_len + 1,
			_Alignof(char)
		);
		name[0] = label_map->addr == addr ? '@' : '~';
		memcpy(&name[1], label_map->name, label_map->name_len);
		block->debug_info.name.str = name;
		block->debug_info.name.len = label_map->name_len + 1;
	}

	block->next = hook_data->blocks;
	hook_data->blocks = block;

	mtx_lock(&buxn_jit_gdb_hook_mtx);

	block->entry.next_entry = __jit_debug_descriptor.first_entry;
	if (block->entry.next_entry) {
		block->entry.next_entry->prev_entry = &block->entry;
	}
	__jit_debug_descriptor.first_entry = &block->entry;

	__jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
	__jit_debug_descriptor.relevant_entry = &block->entry;
	__jit_debug_register_code();
	__jit_debug_descriptor.action_flag = JIT_NOACTION;

	mtx_unlock(&buxn_jit_gdb_hook_mtx);
}

void
buxn_jit_init_gdb_hook(
	struct buxn_jit_dbg_hook_s* hook,
	const buxn_jit_gdb_hook_config_t* config
) {
	call_once(&buxn_jit_gdb_once, buxn_jit_gdb_init);

	buxn_jit_gdb_hook_config_t default_config = { 0 };
	if (config == NULL) { config = &default_config; }

	buxn_jit_dbg_hook_data_t* hook_data = buxn_jit_alloc(
		config->mem_ctx,
		sizeof(buxn_jit_dbg_hook_data_t),
		_Alignof(buxn_jit_dbg_hook_data_t)
	);
	*hook_data = (buxn_jit_dbg_hook_data_t){
		.config = *config,
	};

	*hook = (buxn_jit_dbg_hook_t){
		.register_block = buxn_jit_gdb_register,
		.userdata = hook_data,
	};
}

void
buxn_jit_cleanup_gdb_hook(
	struct buxn_jit_dbg_hook_s* hook
) {
	buxn_jit_dbg_hook_data_t* hook_data = hook->userdata;

	mtx_lock(&buxn_jit_gdb_hook_mtx);

	__jit_debug_descriptor.action_flag = JIT_UNREGISTER_FN;
	for (
		buxn_jit_gdb_info_block_t* itr = hook_data->blocks;
		itr != NULL;
		itr = itr->next
	) {
		if (itr->entry.next_entry) {
			itr->entry.next_entry->prev_entry = itr->entry.prev_entry;
		}
		if (itr->entry.prev_entry) {
			itr->entry.prev_entry->next_entry = itr->entry.next_entry;
		}

		__jit_debug_descriptor.relevant_entry = &itr->entry;
		__jit_debug_register_code();
	}
	__jit_debug_descriptor.action_flag = JIT_NOACTION;

	mtx_unlock(&buxn_jit_gdb_hook_mtx);
}
