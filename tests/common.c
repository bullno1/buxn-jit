#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>
#include <buxn/asm/asm.h>
#include <buxn/devices/console.h>
#include <blog.h>
#include <barena.h>
#include <xincbin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"

struct buxn_asm_file_s {
	const char* content;
	size_t size;
	size_t pos;
};

typedef struct {
	const char* name;
	xincbin_data_t content;
} buxn_vfs_entry_t;

struct buxn_asm_ctx_s {
	buxn_vfs_entry_t* vfs;
	barena_t* arena;
	uint8_t* rom;
};

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
	device_handler_t* handler = vm->config.userdata;
	if (handler != NULL) {
		handler->deo(vm, address, handler->userdata);
	}
}

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	device_handler_t* handler = vm->config.userdata;
	if (handler != NULL) {
		return handler->dei(vm, address, handler->userdata);
	} else {
		return vm->device[address];
	}
}

void*
buxn_jit_alloc(void* ctx, size_t size, size_t alignment) {
	return barena_memalign(ctx, size, alignment);
}


bool
(buxn_asm_str)(
	barena_t* arena,
	uint8_t* rom,
	const char* str,
	const char* file, int line
) {
	return buxn_asm_str_len(arena, rom, str, strlen(str), file, line);
}

bool
buxn_asm_str_len(
	struct barena_s* arena,
	uint8_t* rom,
	const char* str, size_t len,
	const char* file, int line
) {
	buxn_asm_ctx_t basm = {
		.arena = arena,
		.rom = rom,
	};
	barena_snapshot_t snapshot = barena_snapshot(basm.arena);

	int size = snprintf(NULL, 0, "%s:%d", file, line);
	char* filename = barena_malloc(basm.arena, size + 1);
	snprintf(filename, size + 1, "%s:%d", file, line);

	basm.vfs = (buxn_vfs_entry_t[]) {
		{
			.name = filename,
			.content = { .data = (const unsigned char*)str, .size = len }
		},
		{ 0 },
	};

	bool result = buxn_asm(&basm, filename);

	barena_restore(basm.arena, snapshot);

	return result;
}

void
buxn_asm_put_symbol(buxn_asm_ctx_t* ctx, uint16_t addr, const buxn_asm_sym_t* sym) {
}

void
buxn_asm_put_rom(buxn_asm_ctx_t* ctx, uint16_t address, uint8_t value) {
	uint16_t offset = address - 256;
	ctx->rom[offset] = value;
}

void
buxn_asm_report(
	buxn_asm_ctx_t* ctx,
	buxn_asm_report_type_t type,
	const buxn_asm_report_t* report
) {
	blog_level_t level = BLOG_LEVEL_INFO;
	switch (type) {
		case BUXN_ASM_REPORT_ERROR: level = BLOG_LEVEL_ERROR; break;
		case BUXN_ASM_REPORT_WARNING: level = BLOG_LEVEL_WARN; break;
	}
	if (report->token == NULL) {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s", report->message
		);
	} else {
		blog_write(
			level,
			report->region->filename, report->region->range.start.line,
			"%s (`%s`)", report->message, report->token
		);
	}
}

buxn_asm_file_t*
buxn_asm_fopen(buxn_asm_ctx_t* ctx, const char* filename) {
	for (buxn_vfs_entry_t* entry = ctx->vfs; entry->name != NULL; ++entry) {
		if (strcmp(entry->name, filename) == 0) {
			buxn_asm_file_t* file = malloc(sizeof(buxn_asm_file_t));
			file->content = (const char*)entry->content.data;
			file->size = entry->content.size;
			file->pos = 0;
			return file;
		}
	}

	return NULL;
}

void
buxn_asm_fclose(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	free(file);
}

int
buxn_asm_fgetc(buxn_asm_ctx_t* ctx, buxn_asm_file_t* file) {
	(void)ctx;
	if (file->pos >= file->size) {
		return BUXN_ASM_IO_EOF;
	} else {
		return (int)file->content[file->pos++];
	}
}

void*
buxn_asm_alloc(buxn_asm_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign(ctx->arena, size, alignment);
}

void
buxn_system_debug(buxn_vm_t* vm, uint8_t value) {
}

void
buxn_system_set_metadata(buxn_vm_t* vm, uint16_t address) {
	(void)vm;
	(void)address;
}

void
buxn_system_theme_changed(buxn_vm_t* vm) {
	(void)vm;
}

void
buxn_console_handle_write(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stdout);
}

void
buxn_console_handle_error(struct buxn_vm_s* vm, buxn_console_t* device, char c) {
	(void)vm;
	(void)device;
	fputc(c, stderr);
}
