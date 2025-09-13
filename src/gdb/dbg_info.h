#ifndef BUXN_JIT_DBG_INFO_H
#define BUXN_JIT_DBG_INFO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint16_t addr;
	uintptr_t start;
	size_t size;
} buxn_jit_dbg_info_t;

#endif
