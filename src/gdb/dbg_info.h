#ifndef BUXN_JIT_DBG_INFO_H
#define BUXN_JIT_DBG_INFO_H

#include <stdint.h>
#include <stddef.h>

// With this structure, the consumer side only has to copy one extra time if
// the string is available
typedef struct {
	size_t len;
	const char* str;
} buxn_jit_dbg_str_t;

typedef struct {
	uint16_t addr;
	uintptr_t start;
	size_t size;

	buxn_jit_dbg_str_t name;
} buxn_jit_dbg_info_t;

#endif
