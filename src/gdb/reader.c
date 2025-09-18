#include "jit-reader.h"
#include <string.h>
#include <stdlib.h>
#include <barray.h>
#include <bminmax.h>
#include <stdio.h>
#include "dbg_info.h"

#if defined(__x86_64__) || defined(_M_X64)
#	define DWARF_REG_SP 7    // RSP
#	define DWARF_REG_PC 16   // RIP
#elif defined(__i386__) || defined(_M_IX86)
#	define DWARF_REG_SP 4    // EBP
#	define DWARF_REG_PC 8    // EIP
#elif defined(__arm__) || defined(_M_ARM)
#	define DWARF_REG_SP 13   // SP
#	define DWARF_REG_PC 15   // PC
#	define DWARF_REG_LR 14   // LR
#elif defined(__aarch64__)
#	define DWARF_REG_SP 31   // SP
#	define DWARF_REG_PC 32   // PC
#	define DWARF_REG_LR 30   // X30 / LR
#else
#	error "Unsupported architecture"
#endif

#define BUXN_JIT_CHECK(expr) \
	do { \
		if ((expr) != GDB_SUCCESS) { return GDB_FAIL; } \
	} while(0)

typedef struct {
	barray(buxn_jit_dbg_info_t) dbg_info;
} buxn_jit_dbg_reader_t;

typedef struct {
	struct gdb_reader_funcs header;
	buxn_jit_dbg_reader_t reader;
} buxn_jit_dbg_container_t;

static uintptr_t
buxn_jit_uintptr_from_gdb_reg_val(struct gdb_reg_value* value) {
	uintptr_t result;
	memcpy(&result, value->value, value->size);
	value->free(value);
	return result;
}

static void
buxn_jit_free_reg_val(struct gdb_reg_value* value) {
	free(value);
}

static enum gdb_status
buxn_jit_read_target(
	struct gdb_unwind_callbacks* cb,
	uint64_t addr,
	void* value, size_t size
) {
	return cb->target_read(addr, value, size);
}

static struct gdb_reg_value*
buxn_jit_gdb_reg_val_from_uintptr(uintptr_t value) {
	struct gdb_reg_value* result = malloc(sizeof(struct gdb_reg_value) + sizeof(uintptr_t));
	result->defined = 1;
	result->size = sizeof(value);
	result->free = buxn_jit_free_reg_val;
	memcpy(result->value, &value, sizeof(value));
	return result;
}

const buxn_jit_dbg_info_t*
buxn_jit_gdb_lookup_dbg_info(struct gdb_reader_funcs *self, uintptr_t pc) {
	buxn_jit_dbg_reader_t* reader = self->priv_data;
	BARRAY_FOREACH_REF(info, reader->dbg_info) {
		if (info->start <= pc && pc <= (info->start + info->size)) {
			return info;
		}
	}

	return NULL;
}

static enum gdb_status
buxn_jit_gdb_read(
	struct gdb_reader_funcs* self,
	struct gdb_symbol_callbacks* cb,
	void *memory, long memory_sz
) {
	buxn_jit_dbg_reader_t* reader = self->priv_data;

	buxn_jit_dbg_info_t dbg_info = { 0 };
	memcpy(&dbg_info, memory, sizeof(dbg_info));
	barray_push(reader->dbg_info, dbg_info, NULL);

	char name_buf[256];  // uxn names can't be this long
	int len = snprintf(name_buf, sizeof(name_buf), "uxn:0x%04x", dbg_info.addr);
	if (dbg_info.name.len > 0) {
		enum gdb_status status = cb->target_read(
			(uintptr_t)dbg_info.name.str,
			&name_buf[len],
			(int)BMIN(sizeof(name_buf) - 1 - len, dbg_info.name.len)
		);
		if (status == GDB_SUCCESS) {
			name_buf[len + (int)dbg_info.name.len] = '\0';
		} else {
			name_buf[len] = '\0';
		}
	}

	struct gdb_object* obj = cb->object_open(cb);
	if (dbg_info.num_line_mappings > 0) {
		char* filename = NULL;
		uintptr_t previous_file = 0;
		struct gdb_symtab* symtab = cb->symtab_open(cb, obj, "uxn");
		uintptr_t code_start = dbg_info.start;
		barray(struct gdb_line_mapping) gdb_mappings = NULL;

		buxn_jit_dbg_line_mapping_t* mappings = malloc(
			sizeof(buxn_jit_dbg_line_mapping_t) * dbg_info.num_line_mappings
		);
		enum gdb_status status = cb->target_read(
			(uintptr_t)dbg_info.mappings,
			mappings,
			sizeof(buxn_jit_dbg_line_mapping_t) * dbg_info.num_line_mappings
		);
		if (status != GDB_SUCCESS) { goto end_read_mapping; }

		for (int i = 0; i < dbg_info.num_line_mappings; ++i) {
			const buxn_jit_dbg_line_mapping_t* mapping = &mappings[i];
			// Moved to a different file
			if ((uintptr_t)mapping->file.str != previous_file) {
				// Close the previous symtab and make a new one
				if (symtab != NULL) {
					if (barray_len(gdb_mappings) > 0) {
						cb->line_mapping_add(
							cb, symtab,
							barray_len(gdb_mappings),
							gdb_mappings
						);
						barray_clear(gdb_mappings);
					}

					cb->block_open(
						cb, symtab,
						NULL,
						code_start,
						mapping->pc - 1,
						name_buf
					);
					cb->symtab_close(cb, symtab);
				}

				filename = realloc(filename, mapping->file.len + 1);
				enum gdb_status status = cb->target_read(
					(uintptr_t)mapping->file.str,
					filename,
					mapping->file.len
				);
				if (status != GDB_SUCCESS) { goto end_read_mapping; }
				filename[mapping->file.len] = '\0';

				symtab = cb->symtab_open(cb, obj, filename);
				previous_file = (uintptr_t)mapping->file.str;
				code_start = mapping->pc;
			}

			struct gdb_line_mapping gdb_mapping = {
				.line = mapping->line,
				.pc = mapping->pc,
			};
			barray_push(gdb_mappings, gdb_mapping, NULL);
		}

end_read_mapping:
		if (symtab != NULL) {
			if (barray_len(gdb_mappings) > 0) {
				cb->line_mapping_add(
					cb, symtab,
					barray_len(gdb_mappings),
					gdb_mappings
				);
				barray_clear(gdb_mappings);
			}
			cb->block_open(
				cb, symtab,
				NULL,
				code_start,
				dbg_info.start + dbg_info.size,
				name_buf
			);
			cb->symtab_close(cb, symtab);
		}

		free(mappings);
		free(filename);
		barray_free(NULL, gdb_mappings);
	} else {
		struct gdb_symtab* symtab = cb->symtab_open(cb, obj, "uxn");
		cb->block_open(
			cb, symtab,
			NULL,
			dbg_info.start,
			dbg_info.start + dbg_info.size,
			name_buf
		);
		cb->symtab_close(cb, symtab);
	}

	cb->object_close(cb, obj);

	return GDB_SUCCESS;
}

static enum gdb_status
buxn_jit_gdb_unwind(
	struct gdb_reader_funcs* self,
    struct gdb_unwind_callbacks* cb
) {
	uintptr_t sp = buxn_jit_uintptr_from_gdb_reg_val(cb->reg_get(cb, DWARF_REG_SP));
	uintptr_t pc = buxn_jit_uintptr_from_gdb_reg_val(cb->reg_get(cb, DWARF_REG_PC));
	uintptr_t prev_pc, prev_sp;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
	BUXN_JIT_CHECK(buxn_jit_read_target(cb, sp + sizeof(uintptr_t), &prev_pc, sizeof(prev_pc)));
	prev_sp = sp + sizeof(uintptr_t) * 2;
	if (buxn_jit_gdb_lookup_dbg_info(self, pc)) {
		sp = prev_sp;
		BUXN_JIT_CHECK(buxn_jit_read_target(cb, sp + sizeof(uintptr_t), &prev_pc, sizeof(prev_pc)));
		prev_sp = sp + sizeof(uintptr_t) * 2;
	}
#else
#	error "Unsupported architecture"
#endif

    cb->reg_set(cb, DWARF_REG_SP, buxn_jit_gdb_reg_val_from_uintptr(prev_sp));
    cb->reg_set(cb, DWARF_REG_PC, buxn_jit_gdb_reg_val_from_uintptr(prev_pc));
	return GDB_SUCCESS;
}

static struct gdb_frame_id
buxn_jit_gdb_get_frame_id(
	struct gdb_reader_funcs *self,
    struct gdb_unwind_callbacks* cb
) {
	uintptr_t pc = buxn_jit_uintptr_from_gdb_reg_val(cb->reg_get(cb, DWARF_REG_PC));
	uintptr_t sp = buxn_jit_uintptr_from_gdb_reg_val(cb->reg_get(cb, DWARF_REG_SP));
	const buxn_jit_dbg_info_t* info = buxn_jit_gdb_lookup_dbg_info(self, pc);

	return (struct gdb_frame_id){
		.code_address = info != NULL ? info->start : pc,
		.stack_address = sp,
	};
}

static void
buxn_jit_gdb_destroy(struct gdb_reader_funcs *self) {
	buxn_jit_dbg_reader_t* reader = self->priv_data;
	barray_free(NULL, reader->dbg_info);

	free(self);
}

struct gdb_reader_funcs*
gdb_init_reader(void) {
	buxn_jit_dbg_container_t* container = malloc(sizeof(buxn_jit_dbg_container_t));
	*container = (buxn_jit_dbg_container_t){
		.header = {
			.reader_version = GDB_READER_INTERFACE_VERSION,

			.read = buxn_jit_gdb_read,
			.unwind = buxn_jit_gdb_unwind,
			.get_frame_id = buxn_jit_gdb_get_frame_id,
			.destroy = buxn_jit_gdb_destroy,

			.priv_data = &container->reader,
		},
	};

	return &container->header;
}

GDB_DECLARE_GPL_COMPATIBLE_READER

#define BLIB_IMPLEMENTATION
#include <barray.h>
