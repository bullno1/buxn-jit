#ifndef BUXN_JIT_LABEL_MAP_H
#define BUXN_JIT_LABEL_MAP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint16_t addr;
	size_t name_len;
	const char* name;
} buxn_label_map_entry_t;

typedef struct {
	uint16_t size;
	buxn_label_map_entry_t* entries;
} buxn_label_map_t;

static inline const buxn_label_map_entry_t*
buxn_pc_to_label(const buxn_label_map_t* label_map, uint16_t pc) {
	// Find the closest preceding label
	const buxn_label_map_entry_t* closest_entry = NULL;
	for (uint16_t i = 0; i < label_map->size; ++i) {
		const buxn_label_map_entry_t* entry = &label_map->entries[i];
		if (
			entry->addr > 0x00ff  // Not in zero page
			&&
			(entry->name_len > 0 && entry->name[0] != '@')  // Not anonymous
			&&
			entry->addr <= pc
			&&
			entry->addr > (closest_entry != NULL ? closest_entry->addr : 0)
		) {
			closest_entry = entry;
		}
	}

	return closest_entry;
}

#endif
