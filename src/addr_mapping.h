#ifndef BUXN_JIT_ADDR_MAPPING_H
#define BUXN_JIT_ADDR_MAPPING_H

#include <stdint.h>
#include <buxn/dbg/symtab.h>

#define BUXN_JIT_MAPPING_CHUNK_SIZE 16

typedef struct {
	struct buxn_jit_addr_mark_s* mark;
	uint16_t addr;
} buxn_jit_addr_mapping_t;

typedef struct buxn_jit_addr_mapping_chunk_s {
	struct buxn_jit_addr_mapping_chunk_s* next;

	int len;
	buxn_jit_addr_mapping_t	mappings[BUXN_JIT_MAPPING_CHUNK_SIZE];
} buxn_jit_addr_mapping_chunk_t;

typedef struct {
	int len;

	buxn_jit_addr_mapping_chunk_t* first;
	buxn_jit_addr_mapping_chunk_t* last;
} buxn_jit_addr_mapping_list_t;

static inline const buxn_dbg_sym_t*
buxn_dbg_find_symbol(
	const buxn_dbg_symtab_t* symtab,
	uint16_t address,
	// Since we tend to display bytes sequentially in order, the symbols are
	// usually next to each other too and there is no need to search the entire
	// symtab.
	// Instead, start searching from the previous position.
	int* index_hint
) {
	int default_index_hint = 0;
	if (index_hint == NULL) { index_hint = &default_index_hint; }

	// Initial search with hint
	uint32_t index = (uint32_t)*index_hint;
	const buxn_dbg_sym_t* symbol = NULL;
	for (; index < symtab->num_symbols; ++index) {
		if (symtab->symbols[index].type != BUXN_DBG_SYM_OPCODE) { continue; }
		if (
			symtab->symbols[index].addr_min <= address
			&& address <= symtab->symbols[index].addr_max
		) {
			symbol =  &symtab->symbols[index];
			break;
		}

		if (symtab->symbols[index].addr_min > address) {
			break;
		}
	}

	if (symbol != NULL) {
		*index_hint = index;
		return symbol;
	}

	// Second search without hint
	index = 0;
	uint32_t hint = (uint32_t)*index_hint;
	for (; index < hint; ++index) {
		if (symtab->symbols[index].type != BUXN_DBG_SYM_OPCODE) { continue; }
		if (
			symtab->symbols[index].addr_min <= address
			&& address <= symtab->symbols[index].addr_max
		) {
			symbol =  &symtab->symbols[index];
			break;
		}

		if (symtab->symbols[index].addr_min > address) {
			break;
		}
	}

	*index_hint = index;
	return symbol;
}

#endif
