#ifndef BUXN_JIT_TEST_COMMON_H
#define BUXN_JIT_TEST_COMMON_H

#include <stdbool.h>
#include <stdint.h>

struct barena_s;

bool
buxn_asm_str(
	struct barena_s* arena,
	uint8_t* rom,
	const char* str,
	const char* file, int line
);

#define buxn_asm_str(arena, rom, str) buxn_asm_str(arena, rom, str, __FILE__, __LINE__)

#endif
