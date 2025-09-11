// vim: set foldmethod=marker foldlevel=0:
#include <buxn/vm/jit.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/opcodes.h>
#include <sljitLir.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#define BHAMT_HASH_TYPE uint32_t
#include "hamt.h"

#ifndef BUXN_JIT_ASSERT
#	include <assert.h>
#	define BUXN_JIT_ASSERT(condition, msg) assert((condition) && (msg))
#endif

#ifndef BUXN_JIT_VERBOSE
#	define BUXN_JIT_VERBOSE 0
#endif

#if BUXN_JIT_VERBOSE
#	include <stdio.h>
#endif

#define BUXN_JIT_ADDR_EQ(LHS, RHS) (LHS == RHS)
#define BUXN_JIT_MEM() SLJIT_MEM2(SLJIT_R(BUXN_JIT_R_MEM_BASE), SLJIT_R(BUXN_JIT_R_MEM_OFFSET))
#define BUXN_JIT_MEM_OFFSET() SLJIT_R(BUXN_JIT_R_MEM_OFFSET)
#define BUXN_JIT_TMP() SLJIT_R(BUXN_JIT_R_TMP)

#define BUXN_JIT_OP_K 0x80
#define BUXN_JIT_OP_R 0x40
#define BUXN_JIT_OP_2 0x20

enum {
	BUXN_JIT_S_VM = 0,
	BUXN_JIT_S_WSP,
	BUXN_JIT_S_RSP,

	BUXN_JIT_S_COUNT,
};

enum {
	BUXN_JIT_R_MEM_BASE = 0,
	BUXN_JIT_R_MEM_OFFSET,
	BUXN_JIT_R_TMP,

	BUXN_JIT_R_OP_0,
	BUXN_JIT_R_OP_1,
	BUXN_JIT_R_OP_2,
	BUXN_JIT_R_OP_3,
	BUXN_JIT_R_OP_4,
	BUXN_JIT_R_OP_5,

	BUXN_JIT_R_COUNT,
};

enum {
	BUXN_JIT_R_OP_MIN = BUXN_JIT_R_OP_0,
	BUXN_JIT_R_OP_MAX = BUXN_JIT_R_OP_5,
};

#undef BUXN_OPCODE_NAME
#define BUXN_OPCODE_NAME(NAME, K, R, S) NAME
#define BUXN_STRINGIFY(X) BUXN_STRINGIFY2(X)
#define BUXN_STRINGIFY2(X) #X
#define BUXN_JIT_DISPATCH(NAME, VALUE) \
	case VALUE: { \
		if (BUXN_JIT_VERBOSE) { \
			fprintf( \
				stderr, \
				"  ; " BUXN_STRINGIFY2(NAME) "%s%s%s {{{\n", \
				buxn_jit_op_flag_2(ctx) ? "2" : "", \
				buxn_jit_op_flag_k(ctx) ? "k" : "", \
				buxn_jit_op_flag_r(ctx) ? "r" : ""  \
			); \
		} \
		BUXN_CONCAT(buxn_jit_, NAME)(ctx); \
		if (BUXN_JIT_VERBOSE) { \
			fprintf(stderr, "  ; }}}\n"); \
		} \
	} break;

typedef sljit_u32 (*buxn_jit_fn_t)(sljit_up vm);

typedef struct buxn_jit_block_s buxn_jit_block_t;
struct buxn_jit_block_s {
	uint16_t key;
	buxn_jit_block_t* children[BHAMT_NUM_CHILDREN];

	buxn_jit_fn_t fn;
	sljit_uw head_addr;
	sljit_uw body_addr;
	sljit_sw executable_offset;

	buxn_jit_block_t* next;
};

typedef struct {
	buxn_jit_block_t* root;
	buxn_jit_block_t* first;
} buxn_jit_block_map_t;

enum {
	BUXN_JIT_SEM_CONST   = 1 << 0,
	BUXN_JIT_SEM_BOOLEAN = 1 << 1,
};

typedef struct buxn_jit_value_s buxn_jit_value_t;
struct buxn_jit_value_s {
	uint8_t semantics;
	uint8_t const_value;
};

typedef sljit_s32 buxn_jit_reg_t;

typedef struct {
	uint8_t semantics;
	bool is_short;
	uint16_t const_value;
	buxn_jit_reg_t reg;
} buxn_jit_operand_t;

typedef enum {
	BUXN_JIT_LINK_TO_HEAD,
	BUXN_JIT_LINK_TO_BODY,
} buxn_jit_link_type_t;

typedef struct buxn_jit_entry_s buxn_jit_entry_t;
struct buxn_jit_entry_s {
	buxn_jit_entry_t* next;

	buxn_jit_link_type_t link_type;
	buxn_jit_block_t* block;
	struct sljit_compiler* compiler;
	union {
		struct sljit_jump* jump;
		uint16_t pc;
	};
};

struct buxn_jit_s {
	buxn_vm_t* vm;
	buxn_jit_alloc_ctx_t* alloc_ctx;
	buxn_jit_stats_t stats;

	buxn_jit_block_map_t blocks;

	buxn_jit_entry_t* compile_queue;
	buxn_jit_entry_t* link_queue;
	buxn_jit_entry_t* cleanup_queue;
	buxn_jit_entry_t* entry_pool;
};

typedef struct {
	buxn_jit_t* jit;
	buxn_jit_block_t* block;
	struct sljit_compiler* compiler;
	struct sljit_label* head_label;
	struct sljit_label* body_label;
	uint16_t pc;
	uint8_t current_opcode;
	sljit_sw mem_base;
	buxn_jit_value_t wst[256];
	buxn_jit_value_t rst[256];
	uint8_t wsp;
	uint8_t rsp;
	uint8_t* ewsp;
	uint8_t* ersp;
	buxn_jit_reg_t wsp_reg;
	buxn_jit_reg_t rsp_reg;
	uint8_t used_registers;

	buxn_jit_operand_t wst_top;
	buxn_jit_operand_t rst_top;
#if BUXN_JIT_VERBOSE
	int label_id;
#endif
} buxn_jit_ctx_t;

// https://nullprogram.com/blog/2018/07/31/
static inline uint32_t
buxn_jit_prospector32(uint32_t x) {
	x ^= x >> 15;
	x *= 0x2c1b3c6dU;
	x ^= x >> 12;
	x *= 0x297a2d39U;
	x ^= x >> 15;
	return x;
}

static buxn_jit_block_t*
buxn_jit(buxn_jit_t* jit, uint16_t pc);

buxn_jit_t*
buxn_jit_init(buxn_vm_t* vm, buxn_jit_alloc_ctx_t* alloc_ctx) {
	buxn_jit_t* jit = buxn_jit_alloc(alloc_ctx, sizeof(buxn_jit_t), _Alignof(buxn_jit_t));
	*jit = (buxn_jit_t){
		.vm = vm,
		.alloc_ctx = alloc_ctx,
	};
	return jit;
}

buxn_jit_stats_t*
buxn_jit_stats(buxn_jit_t* jit) {
	return &jit->stats;
}

void
buxn_jit_execute(buxn_jit_t* jit, uint16_t pc) {
	while (pc != 0) {
		if (pc >= BUXN_RESET_VECTOR) {
			buxn_jit_block_t* block = buxn_jit(jit, pc);
			pc = (uint16_t)block->fn((uintptr_t)jit->vm);
			jit->stats.num_bounces += (pc != 0);
		} else {
			buxn_vm_execute(jit->vm, pc);
		}
	}
}

void
buxn_jit_cleanup(buxn_jit_t* jit) {
	for (buxn_jit_block_t* itr = jit->blocks.first; itr != NULL; itr = itr->next) {
		if (itr->fn != NULL) {
			sljit_free_code((void*)itr->fn, NULL);
		}
	}
}

static buxn_jit_reg_t
buxn_jit_alloc_reg(buxn_jit_ctx_t* ctx) {
	_Static_assert(
		sizeof(ctx->used_registers) * CHAR_BIT >= (BUXN_JIT_R_OP_MAX - BUXN_JIT_R_OP_MIN + 1),
		"buxn_jit_ctx_t::used_registers needs more bits"
	);

	for (int i = 0; i < (BUXN_JIT_R_OP_MAX - BUXN_JIT_R_OP_MIN + 1); ++i) {
		uint8_t mask = 1 << i;
		if ((ctx->used_registers & mask) == 0) {
			ctx->used_registers |= mask;
#if BUXN_JIT_VERBOSE
			fprintf(stderr, "  ; alloc_reg(r%d)\n", SLJIT_R(BUXN_JIT_R_OP_MIN + i) - SLJIT_R0);
#endif
			return SLJIT_R(BUXN_JIT_R_OP_MIN + i);
		}
	}

	BUXN_JIT_ASSERT(false, "Out of registers");
	return 0;
}

static uint8_t
buxn_jit_reg_mask(buxn_jit_reg_t reg) {
	int reg_no = reg - SLJIT_R0;
	BUXN_JIT_ASSERT(BUXN_JIT_R_OP_MIN <= reg_no && reg_no <= BUXN_JIT_R_OP_MAX, "Invalid register");
	return (uint8_t)1 << (reg_no - BUXN_JIT_R_OP_MIN);
}

static void
buxn_jit_free_reg(buxn_jit_ctx_t* ctx, buxn_jit_reg_t reg) {
#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; free_reg(r%d)\n", reg - SLJIT_R0);
#endif
	uint8_t mask = buxn_jit_reg_mask(reg);
	BUXN_JIT_ASSERT((ctx->used_registers & mask), "Freeing unused register");
	ctx->used_registers &= ~mask;
}

static void
buxn_jit_finalize(buxn_jit_ctx_t* ctx);

static void
buxn_jit_next_opcode(buxn_jit_ctx_t* ctx);

// JIT queue {{{

static buxn_jit_entry_t*
buxn_jit_dequeue(buxn_jit_entry_t** queue) {
	buxn_jit_entry_t* entry = *queue;
	if (entry != NULL) {
		*queue = entry->next;
	}

	return entry;
}

static void
buxn_jit_enqueue(buxn_jit_entry_t** queue, buxn_jit_entry_t* entry) {
	entry->next = *queue;
	*queue = entry;
}

static buxn_jit_entry_t*
buxn_jit_alloc_entry(buxn_jit_t* jit) {
	buxn_jit_entry_t* entry = buxn_jit_dequeue(&jit->entry_pool);
	if (entry == NULL) {
		entry = buxn_jit_alloc(
			jit->alloc_ctx,
			sizeof(buxn_jit_entry_t),
			_Alignof(buxn_jit_entry_t)
		);
	}

	return entry;
}

static buxn_jit_block_t*
buxn_jit_queue_block(buxn_jit_t* jit, uint16_t pc) {
	uint32_t hash = buxn_jit_prospector32(pc);
	buxn_jit_block_t** itr;
	buxn_jit_block_t* block;
	BHAMT_SEARCH(jit->blocks.root, itr, block, hash, pc, BUXN_JIT_ADDR_EQ);
	if (block == NULL) {
		block = *itr = buxn_jit_alloc(
			jit->alloc_ctx,
			sizeof(buxn_jit_block_t),
			_Alignof(buxn_jit_block_t)
		);
		*block = (buxn_jit_block_t){
			.key = pc,
		};
		block->next = jit->blocks.first;
		jit->blocks.first = block;
		++jit->stats.num_blocks;

		struct sljit_compiler* compiler = sljit_create_compiler(NULL);

		buxn_jit_entry_t* compile_entry = buxn_jit_alloc_entry(jit);
		compile_entry->block = block;
		compile_entry->compiler = compiler;
		compile_entry->pc = pc;
		buxn_jit_enqueue(&jit->compile_queue, compile_entry);

		buxn_jit_entry_t* cleanup_entry = buxn_jit_alloc_entry(jit);
		cleanup_entry->compiler = compiler;
		buxn_jit_enqueue(&jit->cleanup_queue, cleanup_entry);
	}

	return block;
}

// }}}

// Micro ops {{{

static inline bool
buxn_jit_op_flag_2(buxn_jit_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_JIT_OP_2;
}

static inline bool
buxn_jit_op_flag_k(buxn_jit_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_JIT_OP_K
		&&
		ctx->current_opcode != 0x20  // JCI
		&&
		ctx->current_opcode != 0x40  // JMI
		&&
		ctx->current_opcode != 0x60  // JSI
		&&
		ctx->current_opcode != 0x80  // LIT
		&&
		ctx->current_opcode != 0xa0  // LIT2
		&&
		ctx->current_opcode != 0xc0  // LITr
		&&
		ctx->current_opcode != 0xe0  // LIT2r
		;
}

static inline bool
buxn_jit_op_flag_r(buxn_jit_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_JIT_OP_R;
}

static inline void
buxn_jit_set_mem_base(buxn_jit_ctx_t* ctx, sljit_sw base) {
	if (ctx->mem_base != base) {
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			SLJIT_R(BUXN_JIT_R_MEM_BASE), 0,
			SLJIT_S(BUXN_JIT_S_VM), 0,
			SLJIT_IMM, base
		);
		ctx->mem_base = base;
	}
}

static void
buxn_jit_do_push(
	buxn_jit_ctx_t* ctx,
	buxn_jit_operand_t operand,
	bool flag_r
) {
	buxn_jit_set_mem_base(
		ctx,
		flag_r ? SLJIT_OFFSETOF(buxn_vm_t, rs) : SLJIT_OFFSETOF(buxn_vm_t, ws)
	);
	buxn_jit_reg_t stack_ptr_reg = flag_r
		? SLJIT_S(BUXN_JIT_S_RSP)
		: SLJIT_S(BUXN_JIT_S_WSP);

	if (operand.is_short) {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM_OFFSET(), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_LSHR,
			BUXN_JIT_TMP(), 0,
			operand.reg, 0,
			SLJIT_IMM, 8
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			BUXN_JIT_TMP(), 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			stack_ptr_reg, 0,
			stack_ptr_reg, 0,
			SLJIT_IMM, 1
		);

		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM_OFFSET(), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_AND,
			BUXN_JIT_TMP(), 0,
			operand.reg, 0,
			SLJIT_IMM, 0xff
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			BUXN_JIT_TMP(), 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			stack_ptr_reg, 0,
			stack_ptr_reg, 0,
			SLJIT_IMM, 1
		);
	} else {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM_OFFSET(), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			operand.reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			stack_ptr_reg, 0,
			stack_ptr_reg, 0,
			SLJIT_IMM, 1
		);
	}
}

static void
buxn_jit_flush_stack(buxn_jit_ctx_t* ctx, buxn_jit_operand_t* stack) {
	bool flag_r = stack == &ctx->rst_top;
	if (stack->reg != 0) {
#if BUXN_JIT_VERBOSE
		fprintf(stderr, "  ; flush %s {{{\n", flag_r ? "RST" : "WST");
#endif
		buxn_jit_do_push(ctx, *stack, flag_r);
		stack->reg = 0;
#if BUXN_JIT_VERBOSE
		fprintf(stderr, "  ; }}}\n");
#endif
	}
}

static void
buxn_jit_flush_stacks(buxn_jit_ctx_t* ctx) {
	buxn_jit_flush_stack(ctx, &ctx->wst_top);
	buxn_jit_flush_stack(ctx, &ctx->rst_top);
}

static void
buxn_jit_push_ex(buxn_jit_ctx_t* ctx, buxn_jit_operand_t operand, bool flag_r) {
#if BUXN_JIT_VERBOSE
	fprintf(
		stderr,
		"  ; push(reg=r%d, flag_2=%d, flag_r=%d)\n",
		operand.reg - SLJIT_R0,
		operand.is_short,
		flag_r
	);
#endif

	BUXN_JIT_ASSERT(
		ctx->used_registers & buxn_jit_reg_mask(operand.reg),
		"Pushing operand with unused register"
	);

	// Defer the entire push operation until it's actually needed
	buxn_jit_operand_t* deferred_push = flag_r ? &ctx->rst_top : &ctx->wst_top;
	if (deferred_push->reg != 0) {
		buxn_jit_flush_stack(ctx, deferred_push);
	}
	*deferred_push = operand;

	buxn_jit_value_t* stack = flag_r ? ctx->rst : ctx->wst;
	uint8_t* stack_ptr = flag_r ? &ctx->rsp : &ctx->wsp;

	if (operand.is_short) {
		buxn_jit_value_t* hi = &stack[(*stack_ptr)++];
		buxn_jit_value_t* lo = &stack[(*stack_ptr)++];
		hi->semantics = lo->semantics = operand.semantics;
		hi->const_value = operand.const_value >> 8;
		lo->const_value = operand.const_value & 0xff;
	} else {
		buxn_jit_value_t* value = &stack[(*stack_ptr)++];
		value->semantics = operand.semantics;
		value->const_value = (uint8_t)operand.const_value;
	}
}

static buxn_jit_operand_t
buxn_jit_pop_ex(buxn_jit_ctx_t* ctx, bool flag_2, bool flag_r) {
#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; pop(flag_2=%d, flag_r=%d) {{{\n", flag_2, flag_r);
#endif

	buxn_jit_value_t* stack = flag_r ? ctx->rst : ctx->wst;
	uint8_t* stack_ptr = flag_r ? ctx->ersp : ctx->ewsp;

	sljit_sw mem_base = flag_r ? SLJIT_OFFSETOF(buxn_vm_t, rs) : SLJIT_OFFSETOF(buxn_vm_t, ws);
	buxn_jit_reg_t stack_ptr_reg = flag_r ? ctx->rsp_reg : ctx->wsp_reg;

	buxn_jit_operand_t operand = {
		.is_short = flag_2,
	};

	buxn_jit_operand_t* cached_operand = flag_r ? &ctx->rst_top : &ctx->wst_top;
	BUXN_JIT_ASSERT(
		(cached_operand->reg == 0) || (ctx->used_registers & buxn_jit_reg_mask(cached_operand->reg)),
		"Cached operand's register is not reserved"
	);

	if (flag_2) {
		buxn_jit_value_t lo = stack[--(*stack_ptr)];
		buxn_jit_value_t hi = stack[--(*stack_ptr)];
		if (
			(hi.semantics & BUXN_JIT_SEM_CONST)
			&&
			(lo.semantics & BUXN_JIT_SEM_CONST)
		) {
			operand.semantics = BUXN_JIT_SEM_CONST;
			operand.const_value = (uint16_t)hi.const_value << 8 | (uint16_t)lo.const_value;
		}

		if (cached_operand->reg != 0 && cached_operand->is_short) {
			operand = *cached_operand;
		} else {
			buxn_jit_flush_stack(ctx, cached_operand);

			operand.reg = buxn_jit_alloc_reg(ctx);
			buxn_jit_set_mem_base(ctx, mem_base);

			sljit_emit_op2(
				ctx->compiler,
				SLJIT_SUB,
				stack_ptr_reg, 0,
				stack_ptr_reg, 0,
				SLJIT_IMM, 1
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				BUXN_JIT_MEM_OFFSET(), 0,
				stack_ptr_reg, 0
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				operand.reg, 0,
				BUXN_JIT_MEM(), 0
			);

			sljit_emit_op2(
				ctx->compiler,
				SLJIT_SUB,
				stack_ptr_reg, 0,
				stack_ptr_reg, 0,
				SLJIT_IMM, 1
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				BUXN_JIT_MEM_OFFSET(), 0,
				stack_ptr_reg, 0
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				BUXN_JIT_TMP(), 0,
				BUXN_JIT_MEM(), 0
			);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_SHL,
				BUXN_JIT_TMP(), 0,
				BUXN_JIT_TMP(), 0,
				SLJIT_IMM, 8
			);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_OR,
				operand.reg, 0,
				operand.reg, 0,
				BUXN_JIT_TMP(), 0
			);
		}
	} else {
		buxn_jit_value_t value = stack[--(*stack_ptr)];
		operand.const_value = value.const_value;
		operand.semantics = value.semantics;

		if (cached_operand->reg != 0 && !cached_operand->is_short) {
			operand = *cached_operand;
		} else {
			buxn_jit_flush_stack(ctx, cached_operand);

			operand.reg = buxn_jit_alloc_reg(ctx);
			buxn_jit_set_mem_base(ctx, mem_base);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_SUB,
				stack_ptr_reg, 0,
				stack_ptr_reg, 0,
				SLJIT_IMM, 1
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				BUXN_JIT_MEM_OFFSET(), 0,
				stack_ptr_reg, 0
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				operand.reg, 0,
				BUXN_JIT_MEM(), 0
			);
		}
	}

	cached_operand->reg = 0;

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; }}} => r%d\n", operand.reg - SLJIT_R0);
#endif

	return operand;
}

static void
buxn_jit_push(buxn_jit_ctx_t* ctx, buxn_jit_operand_t operand) {
	buxn_jit_push_ex(ctx, operand, buxn_jit_op_flag_r(ctx));
}

static buxn_jit_operand_t
buxn_jit_pop(buxn_jit_ctx_t* ctx) {
	return buxn_jit_pop_ex(ctx, buxn_jit_op_flag_2(ctx), buxn_jit_op_flag_r(ctx));
}

static buxn_jit_operand_t
buxn_jit_load(
	buxn_jit_ctx_t* ctx,
	buxn_jit_reg_t reg,
	buxn_jit_operand_t addr
) {
	buxn_jit_operand_t result = {
		.is_short = buxn_jit_op_flag_2(ctx),
		.reg = reg,
	};
#if BUXN_JIT_VERBOSE
	fprintf(
		stderr,
		"  ; r%d = load(addr=r%d(0x%04x), flag_2=%d)\n",
		result.reg - SLJIT_R0,
		addr.reg - SLJIT_R0,
		addr.const_value,
		result.is_short
	);
#endif

	buxn_jit_set_mem_base(ctx, SLJIT_OFFSETOF(buxn_vm_t, memory));
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U16,
		BUXN_JIT_MEM_OFFSET(), 0,
		addr.reg, 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		result.reg, 0,
		BUXN_JIT_MEM(), 0
	);

	if (result.is_short) {
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_SHL,
			result.reg, 0,
			result.reg, 0,
			SLJIT_IMM, 8
		);

		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, 1
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_AND,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, addr.is_short ? 0xffff : 0x00ff
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_TMP(), 0,
			BUXN_JIT_MEM(), 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_OR,
			result.reg, 0,
			result.reg, 0,
			BUXN_JIT_TMP(), 0
		);
	}

	buxn_jit_free_reg(ctx, addr.reg);
	return result;
}

static void
buxn_jit_store(
	buxn_jit_ctx_t* ctx,
	buxn_jit_operand_t addr,
	buxn_jit_operand_t value
) {
#if BUXN_JIT_VERBOSE
	fprintf(
		stderr,
		"  ; store(addr=r%d(0x%040x), value=r%d, flag_2=%d)\n",
		addr.reg - SLJIT_R0,
		addr.const_value,
		value.reg - SLJIT_R0,
		value.is_short
	);
#endif

	buxn_jit_set_mem_base(ctx, SLJIT_OFFSETOF(buxn_vm_t, memory));

	if (value.is_short) {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U16,
			BUXN_JIT_MEM_OFFSET(), 0,
			addr.reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_LSHR,
			BUXN_JIT_TMP(), 0,
			value.reg, 0,
			SLJIT_IMM, 8
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			BUXN_JIT_TMP(), 0
		);

		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, 1
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_AND,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, addr.is_short ? 0xffff : 0x00ff
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			value.reg, 0
		);
	} else {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U16,
			BUXN_JIT_MEM_OFFSET(), 0,
			addr.reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			value.reg, 0
		);
	}

	buxn_jit_free_reg(ctx, addr.reg);
	buxn_jit_free_reg(ctx, value.reg);
}

static void
buxn_jit_load_state(buxn_jit_ctx_t* ctx) {
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_S(BUXN_JIT_S_WSP), 0,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, wsp)
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_S(BUXN_JIT_S_RSP), 0,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, rsp)
	);
}

static void
buxn_jit_save_state(buxn_jit_ctx_t* ctx) {
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, wsp),
		SLJIT_S(BUXN_JIT_S_WSP), 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, rsp),
		SLJIT_S(BUXN_JIT_S_RSP), 0
	);
}

static void
buxn_jit_jump_abs(buxn_jit_ctx_t* ctx, buxn_jit_operand_t target, uint16_t return_addr) {
	struct sljit_jump* exit = NULL;
#if BUXN_JIT_VERBOSE
	int exit_id = 0;
#endif

	// TODO: Improve indirect (non-const) call
	// Call into a trampoline helper function
	if (target.semantics & BUXN_JIT_SEM_CONST) {
		if (return_addr == 0) {
			// Recheck assumed constant value before jumping
			struct sljit_jump* jump = sljit_emit_cmp(
				ctx->compiler,
				SLJIT_EQUAL | SLJIT_REWRITABLE_JUMP,
				target.reg, 0,
				SLJIT_IMM, target.const_value
			);
#if BUXN_JIT_VERBOSE
			fprintf(stderr, "  ; jump => here\n");
#endif
			sljit_set_label(jump, sljit_emit_label(ctx->compiler));

			buxn_jit_entry_t* entry = buxn_jit_alloc_entry(ctx->jit);
			entry->link_type = BUXN_JIT_LINK_TO_BODY;
			entry->block = buxn_jit_queue_block(ctx->jit, target.const_value);
			entry->compiler = ctx->compiler;
			entry->jump = jump;
			buxn_jit_enqueue(&ctx->jit->link_queue, entry);
		} else {
			// Recheck assumed constant value before calling
			struct sljit_jump* skip_call = sljit_emit_cmp(
				ctx->compiler,
				SLJIT_NOT_EQUAL,
				target.reg, 0,
				SLJIT_IMM, target.const_value
			);
#if BUXN_JIT_VERBOSE
			int skip_id = ctx->label_id++;
			fprintf(stderr, "  ; jump => label%d\n", skip_id);
#endif

			// This is because we can return from this call and continue execution
			// The target is compiled by a different buxn_jit_ctx_t so upon return,
			// the cached states will be invalid.
			//
			// Other remote jumps do not have to care about cache invalidation
			// since execution will never return to this function.
			ctx->mem_base = 0;
			struct sljit_jump* call = sljit_emit_call(
				ctx->compiler,
				SLJIT_CALL_REG_ARG | SLJIT_REWRITABLE_JUMP,
				SLJIT_ARGS0(32)
			);
#if BUXN_JIT_VERBOSE
			int call_id = ctx->label_id++;
			fprintf(stderr, "  ; jump => label%d\n", call_id);
#endif

			// If the return address is not as expected, trampoline
			exit = sljit_emit_cmp(
				ctx->compiler,
				SLJIT_EQUAL,
				SLJIT_R0, 0,
				SLJIT_IMM, return_addr
			);
#if BUXN_JIT_VERBOSE
			exit_id = ctx->label_id++;
			fprintf(stderr, "  ; jump => label%d\n", exit_id);
#endif
			sljit_emit_return(ctx->compiler, SLJIT_MOV32, SLJIT_R0, 0);

			// Fallback stub in case we can't call for whatever reason
#if BUXN_JIT_VERBOSE
			fprintf(stderr, "  ; label%d:\n", call_id);
#endif
			sljit_set_label(call, sljit_emit_label(ctx->compiler));
			sljit_emit_enter(
				ctx->compiler,
				SLJIT_ENTER_KEEP(BUXN_JIT_S_COUNT) | SLJIT_ENTER_REG_ARG,
				SLJIT_ARGS0(32),
				BUXN_JIT_R_COUNT,
				BUXN_JIT_S_COUNT,
				0
			);
			sljit_emit_return(ctx->compiler, SLJIT_MOV32, SLJIT_IMM, target.const_value);

#if BUXN_JIT_VERBOSE
			fprintf(stderr, "  ; label%d:\n", skip_id);
#endif
			sljit_set_label(skip_call, sljit_emit_label(ctx->compiler));

			buxn_jit_entry_t* entry = buxn_jit_alloc_entry(ctx->jit);
			entry->link_type = BUXN_JIT_LINK_TO_HEAD;
			entry->block = buxn_jit_queue_block(ctx->jit, target.const_value);
			entry->compiler = ctx->compiler;
			entry->jump = call;
			buxn_jit_enqueue(&ctx->jit->link_queue, entry);
		}
	}

	// Return to trampoline.
	// This is always correct but slow.
	sljit_emit_return(ctx->compiler, SLJIT_MOV32, target.reg, 0);

	if (exit != NULL) {
#if BUXN_JIT_VERBOSE
		fprintf(stderr, "  ; label%d:\n", exit_id);
#endif
		sljit_set_label(exit, sljit_emit_label(ctx->compiler));
	}
}

static void
buxn_jit_jump(buxn_jit_ctx_t* ctx, buxn_jit_operand_t target, uint16_t return_addr) {
	buxn_jit_flush_stacks(ctx);

#if BUXN_JIT_VERBOSE
	fprintf(
		stderr,
		"  ; jump(reg=r%d, addr=0x%04x, short=%d, return_addr=0x%04x)\n",
		target.reg - SLJIT_R0,
		target.const_value,
		target.is_short,
		return_addr
	);
#endif

	if (target.is_short) {
		buxn_jit_jump_abs(ctx, target, return_addr);
	} else {
		if (target.semantics & BUXN_JIT_SEM_BOOLEAN) {
			struct sljit_jump* skip_next_opcode = sljit_emit_cmp(
				ctx->compiler,
				SLJIT_NOT_EQUAL,
				target.reg, 0,
				SLJIT_IMM, 0
			);
#if BUXN_JIT_VERBOSE
	int label_id = ctx->label_id++;
	fprintf(stderr, "  ; jump => label%d\n", label_id);
#endif

			buxn_jit_next_opcode(ctx);

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; label%d:\n", label_id);
#endif
			sljit_set_label(skip_next_opcode, sljit_emit_label(ctx->compiler));
		} else {
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_S8,
				target.reg, 0,
				target.reg, 0
			);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_ADD,
				target.reg, 0,
				target.reg, 0,
				SLJIT_IMM, ctx->pc
			);
			target.const_value = (uint16_t)((int32_t)ctx->pc + (int32_t)(int8_t)target.const_value);

			buxn_jit_jump_abs(ctx, target, return_addr);
		}
	}

	buxn_jit_free_reg(ctx, target.reg);
}

static void
buxn_jit_conditional_jump(
	buxn_jit_ctx_t* ctx,
	buxn_jit_operand_t condition,
	buxn_jit_operand_t target
) {
	buxn_jit_flush_stacks(ctx);

	sljit_emit_op2u(
		ctx->compiler,
		SLJIT_AND | SLJIT_SET_Z,
		condition.reg, 0,
		SLJIT_IMM, 0xff
	);
	buxn_jit_free_reg(ctx, condition.reg);

	struct sljit_jump* skip_jump = sljit_emit_jump(ctx->compiler, SLJIT_ZERO);
#if BUXN_JIT_VERBOSE
	int label_id = ctx->label_id++;
	fprintf(stderr, "  ; jump => label%d\n", label_id);
#endif

	buxn_jit_jump(ctx, target, 0);

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; label%d:\n", label_id);
#endif
	sljit_set_label(skip_jump, sljit_emit_label(ctx->compiler));
}

static void
buxn_jit_finalize(buxn_jit_ctx_t* ctx) {
	buxn_jit_block_t* block = ctx->block;
	block->fn = (buxn_jit_fn_t)sljit_generate_code(ctx->compiler, 0, NULL);
	block->head_addr = sljit_get_label_addr(ctx->head_label);
	block->body_addr = sljit_get_label_addr(ctx->body_label);
	block->executable_offset = sljit_get_executable_offset(ctx->compiler);

	ctx->compiler = NULL;
}

static buxn_jit_operand_t
buxn_jit_immediate(buxn_jit_ctx_t* ctx, bool is_short) {
	buxn_jit_operand_t imm = {
		// We will assume that it is a constant, even if it can be overwritten.
		// Jump opcodes will recheck the assumption so it is safe.
		.semantics = BUXN_JIT_SEM_CONST,
		.is_short = is_short,
		.reg = buxn_jit_alloc_reg(ctx),
	};
#if BUXN_JIT_VERBOSE
	fprintf(
		stderr,
		"  ; r%d = rom(addr=0x%04x, flag_2=%d)\n",
		imm.reg - SLJIT_R0,
		ctx->pc,
		imm.is_short
	);
#endif

	if (is_short) {
		uint8_t hi = ctx->jit->vm->memory[(uint16_t)(ctx->pc + 0)];
		uint8_t lo = ctx->jit->vm->memory[(uint16_t)(ctx->pc + 1)];
		imm.const_value = (uint16_t)hi << 8 | (uint16_t)lo;

		buxn_jit_set_mem_base(ctx, SLJIT_OFFSETOF(buxn_vm_t, memory));

		if (ctx->pc < 0xffff) {  // No wrap around
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U16,
				BUXN_JIT_MEM_OFFSET(), 0,
				SLJIT_IMM, ctx->pc
			);
			sljit_emit_mem(
				ctx->compiler,
				SLJIT_MOV_U16 | SLJIT_MEM_LOAD | SLJIT_MEM_UNALIGNED,
				imm.reg,
				BUXN_JIT_MEM(), 0
			);
#if SLJIT_LITTLE_ENDIAN
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_REV_U16,
				imm.reg, 0,
				imm.reg, 0
			);
#endif
		} else {
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U16,
				BUXN_JIT_MEM_OFFSET(), 0,
				SLJIT_IMM, ctx->pc
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				imm.reg, 0,
				BUXN_JIT_MEM(), 0
			);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_SHL,
				imm.reg, 0,
				imm.reg, 0,
				SLJIT_IMM, 8
			);

			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U16,
				BUXN_JIT_MEM_OFFSET(), 0,
				SLJIT_IMM, ctx->pc + 1
			);
			sljit_emit_op1(
				ctx->compiler,
				SLJIT_MOV_U8,
				BUXN_JIT_TMP(), 0,
				BUXN_JIT_MEM(), 0
			);
			sljit_emit_op2(
				ctx->compiler,
				SLJIT_OR,
				imm.reg, 0,
				imm.reg, 0,
				BUXN_JIT_TMP(), 0
			);
		}

		ctx->pc += 2;
	} else {
		imm.const_value = ctx->jit->vm->memory[ctx->pc];

		buxn_jit_set_mem_base(ctx, SLJIT_OFFSETOF(buxn_vm_t, memory));
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U16,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, ctx->pc
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			imm.reg, 0,
			BUXN_JIT_MEM(), 0
		);

		ctx->pc += 1;
	}

	return imm;
}

static buxn_jit_operand_t
buxn_jit_immediate_jump_target(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_immediate(ctx, true);

	target.const_value += ctx->pc;

	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		target.reg, 0,
		target.reg, 0,
		SLJIT_IMM, ctx->pc
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_AND,
		target.reg, 0,
		target.reg, 0,
		SLJIT_IMM, 0xffff
	);

	return target;
}

// }}}

// Opcodes {{{

static void
buxn_jit_BRK(buxn_jit_ctx_t* ctx) {
	buxn_jit_flush_stacks(ctx);
	sljit_emit_return(ctx->compiler, SLJIT_MOV32, SLJIT_IMM, 0);
	buxn_jit_finalize(ctx);
}

static void
buxn_jit_INC(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t operand = buxn_jit_pop(ctx);
	operand.semantics &= ~BUXN_JIT_SEM_BOOLEAN;

	if (operand.semantics & BUXN_JIT_SEM_CONST) {
		operand.const_value += 1;
	}

	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		operand.reg, 0,
		operand.reg, 0,
		SLJIT_IMM, 1
	);

	buxn_jit_push(ctx, operand);
}

static void
buxn_jit_POP(buxn_jit_ctx_t* ctx) {
	if (buxn_jit_op_flag_k(ctx)) { return; }  // POPk is nop

	uint8_t size = buxn_jit_op_flag_2(ctx) ? 2 : 1;
	if (buxn_jit_op_flag_r(ctx)) {
		buxn_jit_flush_stack(ctx, &ctx->rst_top);
		ctx->rsp -= size;
	} else {
		buxn_jit_flush_stack(ctx, &ctx->wst_top);
		ctx->wsp -= size;
	}

	buxn_jit_reg_t stack_ptr_reg = buxn_jit_op_flag_r(ctx)
		? SLJIT_S(BUXN_JIT_S_RSP)
		: SLJIT_S(BUXN_JIT_S_WSP);

	sljit_emit_op2(
		ctx->compiler,
		SLJIT_SUB,
		stack_ptr_reg, 0,
		stack_ptr_reg, 0,
		SLJIT_IMM, size
	);
}

static void
buxn_jit_NIP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_POP(ctx);
	buxn_jit_push(ctx, b);
}

static void
buxn_jit_SWP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_ROT(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t c = buxn_jit_pop(ctx);
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, c);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_DUP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t a = buxn_jit_pop(ctx);
	buxn_jit_push(ctx, a);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_OVR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);
	buxn_jit_push(ctx, a);
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_EQU(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.semantics = BUXN_JIT_SEM_BOOLEAN,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2u(
		ctx->compiler,
		SLJIT_SUB | SLJIT_SET_Z,
		a.reg, 0,
		b.reg, 0
	);
	sljit_emit_op_flags(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_EQUAL
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_NEQ(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.semantics = BUXN_JIT_SEM_BOOLEAN,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2u(
		ctx->compiler,
		SLJIT_SUB | SLJIT_SET_Z,
		a.reg, 0,
		b.reg, 0
	);
	sljit_emit_op_flags(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_NOT_EQUAL
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_GTH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.semantics = BUXN_JIT_SEM_BOOLEAN,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2u(
		ctx->compiler,
		SLJIT_SUB | SLJIT_SET_GREATER,
		a.reg, 0,
		b.reg, 0
	);
	sljit_emit_op_flags(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_GREATER
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_LTH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.semantics = BUXN_JIT_SEM_BOOLEAN,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2u(
		ctx->compiler,
		SLJIT_SUB | SLJIT_SET_LESS,
		a.reg, 0,
		b.reg, 0
	);
	sljit_emit_op_flags(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_LESS
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_JMP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_pop(ctx);
	buxn_jit_jump(ctx, target, 0);
	buxn_jit_finalize(ctx);
}

static void
buxn_jit_JCN(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_pop(ctx);
	buxn_jit_operand_t condition = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));

	buxn_jit_conditional_jump(ctx, condition, target);
}

static void
buxn_jit_JSR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_pop(ctx);
	buxn_jit_operand_t pc = {
		.is_short = true,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U16,
		pc.reg, 0,
		SLJIT_IMM, ctx->pc
	);
	buxn_jit_push_ex(ctx, pc, !buxn_jit_op_flag_r(ctx));
	buxn_jit_jump(ctx, target, ctx->pc);
}

static void
buxn_jit_STH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t a = buxn_jit_pop(ctx);
	buxn_jit_push_ex(ctx, a, !buxn_jit_op_flag_r(ctx));
}

static void
buxn_jit_LDZ(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_load(ctx, buxn_jit_alloc_reg(ctx), addr);
	buxn_jit_push(ctx, value);
}

static void
buxn_jit_STZ(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_pop(ctx);
	buxn_jit_store(ctx, addr, value);
}

static void
buxn_jit_LDR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_S8,
		addr.reg, 0,
		addr.reg, 0
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		addr.reg, 0,
		addr.reg, 0,
		SLJIT_IMM, ctx->pc
	);
	addr.is_short = true;
	buxn_jit_operand_t value = buxn_jit_load(ctx, buxn_jit_alloc_reg(ctx), addr);
	buxn_jit_push(ctx, value);
}

static void
buxn_jit_STR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_pop(ctx);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_S8,
		addr.reg, 0,
		addr.reg, 0
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		addr.reg, 0,
		addr.reg, 0,
		SLJIT_IMM, ctx->pc
	);
	addr.is_short = true;
	buxn_jit_store(ctx, addr, value);
}

static void
buxn_jit_LDA(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, true, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_load(ctx, buxn_jit_alloc_reg(ctx), addr);
	buxn_jit_push(ctx, value);
}

static void
buxn_jit_STA(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, true, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_pop(ctx);
	buxn_jit_store(ctx, addr, value);
}

static sljit_u32
buxn_jit_dei_helper(sljit_up vm, sljit_u32 addr) {
	return buxn_vm_dei((buxn_vm_t*)vm, (uint8_t)addr);
}

static sljit_u32
buxn_jit_dei2_helper(sljit_up vm, sljit_u32 addr) {
	uint8_t hi = buxn_vm_dei((buxn_vm_t*)vm, (uint8_t)(addr + 0));
	uint8_t lo = buxn_vm_dei((buxn_vm_t*)vm, (uint8_t)(addr + 1));
	return (uint16_t)hi << 8 | (uint16_t)lo;
}

static void
buxn_jit_DEI(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t result = {
		.is_short = buxn_jit_op_flag_2(ctx),
		.reg = buxn_jit_alloc_reg(ctx),
	};

	buxn_jit_flush_stacks(ctx);
	buxn_jit_save_state(ctx);
	ctx->mem_base = 0;
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_P,
		SLJIT_R0, 0,
		SLJIT_S(BUXN_JIT_S_VM), 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R1, 0,
		addr.reg, 0
	);
	buxn_jit_free_reg(ctx, addr.reg);
	sljit_emit_icall(
		ctx->compiler,
		SLJIT_CALL,
		SLJIT_ARGS2(32, P, 32),
		SLJIT_IMM, result.is_short
			? SLJIT_FUNC_ADDR(buxn_jit_dei2_helper)
			: SLJIT_FUNC_ADDR(buxn_jit_dei_helper)
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		result.reg, 0,
		SLJIT_R0, 0
	);
	buxn_jit_load_state(ctx);

	buxn_jit_push(ctx, result);
}

static void
buxn_jit_deo_helper(sljit_up vm, sljit_u32 addr) {
	buxn_vm_deo((buxn_vm_t*)vm, (uint8_t)addr);
}

static void
buxn_jit_deo2_helper(sljit_up vm, sljit_u32 addr) {
	buxn_vm_deo((buxn_vm_t*)vm, (uint8_t)(addr + 0));
	buxn_vm_deo((buxn_vm_t*)vm, (uint8_t)(addr + 1));
}

static void
buxn_jit_DEO(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t addr = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));
	buxn_jit_operand_t value = buxn_jit_pop(ctx);

	buxn_jit_set_mem_base(ctx, SLJIT_OFFSETOF(buxn_vm_t, device));
	if (value.is_short) {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM_OFFSET(), 0,
			addr.reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_LSHR,
			BUXN_JIT_TMP(), 0,
			value.reg, 0,
			SLJIT_IMM, 8
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			BUXN_JIT_TMP(), 0
		);

		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, 1
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_AND,
			BUXN_JIT_MEM_OFFSET(), 0,
			BUXN_JIT_MEM_OFFSET(), 0,
			SLJIT_IMM, 0xff
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			value.reg, 0
		);
	} else {
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM_OFFSET(), 0,
			addr.reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			value.reg, 0
		);
	}
	buxn_jit_free_reg(ctx, value.reg);

	buxn_jit_flush_stacks(ctx);
	buxn_jit_save_state(ctx);
	ctx->mem_base = 0;
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_P,
		SLJIT_R0, 0,
		SLJIT_S(BUXN_JIT_S_VM), 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R1, 0,
		addr.reg, 0
	);
	buxn_jit_free_reg(ctx, addr.reg);
	sljit_emit_icall(
		ctx->compiler,
		SLJIT_CALL,
		SLJIT_ARGS2V(P, 32),
		SLJIT_IMM, value.is_short
			? SLJIT_FUNC_ADDR(buxn_jit_deo2_helper)
			: SLJIT_FUNC_ADDR(buxn_jit_deo_helper)
	);
	buxn_jit_load_state(ctx);
}

static void
buxn_jit_ADD(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value + b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_SUB(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value - b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_SUB,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_MUL(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value * b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_MUL,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_DIV(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	if ((c.semantics & BUXN_JIT_SEM_CONST) && (b.const_value != 0)) {
		c.const_value = a.const_value / b.const_value;
	}

	struct sljit_jump* set_zero = sljit_emit_cmp(
		ctx->compiler,
		SLJIT_EQUAL,
		b.reg, 0,
		SLJIT_IMM, 0
	);
#if BUXN_JIT_VERBOSE
	int set_zero_id = ctx->label_id++;
	fprintf(stderr, "  ; jump => label%d\n", set_zero_id);
#endif

	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		BUXN_JIT_TMP(), 0,
		SLJIT_R0, 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R0, 0,
		a.reg, 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R1, 0,
		b.reg, 0
	);
	sljit_emit_op0(ctx->compiler, SLJIT_DIV_UW);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_R0, 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R0, 0,
		BUXN_JIT_TMP(), 0
	);
	struct sljit_jump* end = sljit_emit_jump(ctx->compiler, SLJIT_JUMP);
#if BUXN_JIT_VERBOSE
	int end_id = ctx->label_id++;
	fprintf(stderr, "  ; jump => label%d\n", end_id);
#endif

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; label%d:\n", set_zero_id);
#endif
	sljit_set_label(set_zero, sljit_emit_label(ctx->compiler));
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		c.reg, 0,
		SLJIT_IMM, 0
	);

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; label%d:\n", set_zero_id);
#endif
	sljit_set_label(end, sljit_emit_label(ctx->compiler));

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_AND(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value & b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_AND,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_ORA(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value | b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_OR,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_EOR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx);
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value ^ b.const_value,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_XOR,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_SFT(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop_ex(ctx, false, buxn_jit_op_flag_r(ctx));;
	buxn_jit_operand_t a = buxn_jit_pop(ctx);

	buxn_jit_operand_t c = {
		.is_short = a.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = (a.const_value >> (b.const_value & 0x0f)) << ((b.const_value & 0xf0) >> 4),
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_AND,
		BUXN_JIT_TMP(), 0,
		b.reg, 0,
		SLJIT_IMM, 0x0f
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_LSHR,
		c.reg, 0,
		a.reg, 0,
		BUXN_JIT_TMP(), 0
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_LSHR,
		BUXN_JIT_TMP(), 0,
		b.reg, 0,
		SLJIT_IMM, 4
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_SHL,
		c.reg, 0,
		c.reg, 0,
		BUXN_JIT_TMP(), 0
	);

	buxn_jit_free_reg(ctx, a.reg);
	buxn_jit_free_reg(ctx, b.reg);
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_JCI(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t condition = buxn_jit_pop_ex(ctx, false, false);
	buxn_jit_operand_t target = buxn_jit_immediate_jump_target(ctx);
	buxn_jit_conditional_jump(ctx, condition, target);
}

static void
buxn_jit_JMI(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_immediate_jump_target(ctx);
	buxn_jit_jump(ctx, target, 0);
	buxn_jit_finalize(ctx);
}

static void
buxn_jit_JSI(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t target = buxn_jit_immediate_jump_target(ctx);
	buxn_jit_operand_t pc = {
		.is_short = true,
		.reg = buxn_jit_alloc_reg(ctx),
	};
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U16,
		pc.reg, 0,
		SLJIT_IMM, ctx->pc
	);
	buxn_jit_push_ex(ctx, pc, true);
	buxn_jit_jump(ctx, target, ctx->pc);
}

static void
buxn_jit_LIT(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t lit = buxn_jit_immediate(ctx, buxn_jit_op_flag_2(ctx));
	buxn_jit_push(ctx, lit);
}

// }}}

static void
buxn_jit_compile(buxn_jit_t* jit, const buxn_jit_entry_t* entry) {
	buxn_jit_ctx_t ctx = {
		.jit = jit,
		.pc = entry->pc,
		.block = entry->block,
		.compiler = entry->compiler,
	};

#if BUXN_JIT_VERBOSE
	sljit_compiler_verbose(ctx.compiler, stderr);
	fprintf(stderr, "  ; 0x%04x {{{\n", entry->pc);

	fprintf(stderr, "  ; Prologue {{{\n");
#endif

	// C-compatible prologue
	sljit_emit_enter(
		ctx.compiler,
		0,
		SLJIT_ARGS1(32, P),
		BUXN_JIT_R_COUNT,
		BUXN_JIT_S_COUNT,
		0
	);
	buxn_jit_load_state(&ctx);
	struct sljit_jump* call = sljit_emit_call(
		ctx.compiler,
		SLJIT_CALL_REG_ARG,
		SLJIT_ARGS0(32)
	);
	buxn_jit_save_state(&ctx);
	sljit_emit_return(ctx.compiler, SLJIT_MOV32, SLJIT_R0, 0);

	// sljit-specific fast calling convention
	ctx.head_label = sljit_emit_label(ctx.compiler);
#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; }}}\n");
#endif
	sljit_set_label(call, ctx.head_label);
	sljit_emit_enter(
		ctx.compiler,
		SLJIT_ENTER_KEEP(BUXN_JIT_S_COUNT) | SLJIT_ENTER_REG_ARG,
		SLJIT_ARGS0(32),
		BUXN_JIT_R_COUNT,
		BUXN_JIT_S_COUNT,
		0
	);
	ctx.body_label = sljit_emit_label(ctx.compiler);

	while (ctx.compiler != NULL) {
		buxn_jit_next_opcode(&ctx);
	}

#if BUXN_JIT_VERBOSE
	fprintf(stderr, "  ; }}}\n");
#endif
}

static buxn_jit_block_t*
buxn_jit(buxn_jit_t* jit, uint16_t pc) {
	buxn_jit_block_t* block = buxn_jit_queue_block(jit, pc);
	if (block->fn != NULL) { return block; }

	buxn_jit_entry_t* entry;

	while ((entry = buxn_jit_dequeue(&jit->compile_queue)) != NULL) {
		buxn_jit_compile(jit, entry);
		buxn_jit_enqueue(&jit->entry_pool, entry);
	}

	while ((entry = buxn_jit_dequeue(&jit->link_queue)) != NULL) {
		sljit_uw target = entry->link_type == BUXN_JIT_LINK_TO_HEAD
			? entry->block->head_addr
			: entry->block->body_addr;
		if (target != 0) {
			sljit_set_jump_addr(
				sljit_get_jump_addr(entry->jump),
				target,
				entry->block->executable_offset
			);
		}

		buxn_jit_enqueue(&jit->entry_pool, entry);
	}

	while ((entry = buxn_jit_dequeue(&jit->cleanup_queue)) != NULL) {
		sljit_free_compiler(entry->compiler);
		buxn_jit_enqueue(&jit->entry_pool, entry);
	}

	return block;
}

static void
buxn_jit_next_opcode(buxn_jit_ctx_t* ctx) {
	if (ctx->pc < 256) {
		buxn_jit_flush_stacks(ctx);
		sljit_emit_return(ctx->compiler, SLJIT_MOV32, SLJIT_IMM, ctx->pc);
		buxn_jit_finalize(ctx);
		return;
	}

	ctx->used_registers = 0;
	// Retain the top stack cache operand so we can avoid popping something that
	// was recently pushed
	if (ctx->wst_top.reg != 0) {
		ctx->used_registers |= buxn_jit_reg_mask(ctx->wst_top.reg);
	}
	if (ctx->rst_top.reg != 0) {
		ctx->used_registers |= buxn_jit_reg_mask(ctx->rst_top.reg);
	}
#if BUXN_JIT_VERBOSE
	if (ctx->used_registers != 0) {
		fprintf(stderr, "  ; Retained registers:");
		for (int i = 0; i < (BUXN_JIT_R_OP_MAX - BUXN_JIT_R_OP_MIN + 1); ++i) {
			uint8_t mask = 1 << i;
			if (ctx->used_registers & mask) {
				fprintf(
					stderr,
					" r%d",
					SLJIT_R(BUXN_JIT_R_OP_MIN + i) - SLJIT_R0
				);
			}
		}
		fprintf(stderr, "\n");
	}
#endif

	uint8_t shadow_wsp;
	uint8_t shadow_rsp;
	ctx->current_opcode = ctx->jit->vm->memory[ctx->pc++];

	if (buxn_jit_op_flag_k(ctx)) {
#if BUXN_JIT_VERBOSE
		fprintf(stderr, "  ; Shadow stack {{{\n");
#endif
		if (ctx->wst_top.reg != 0) {
			buxn_jit_reg_t reg = ctx->wst_top.reg;
			buxn_jit_flush_stack(ctx, &ctx->wst_top);
			buxn_jit_free_reg(ctx, reg);
		}
		if (ctx->rst_top.reg != 0) {
			buxn_jit_reg_t reg = ctx->rst_top.reg;
			buxn_jit_flush_stack(ctx, &ctx->rst_top);
			buxn_jit_free_reg(ctx, reg);
		}

		shadow_wsp = ctx->wsp;
		shadow_rsp = ctx->rsp;
		ctx->ewsp = &shadow_wsp;
		ctx->ersp = &shadow_rsp;
		buxn_jit_reg_t swsp_reg = buxn_jit_alloc_reg(ctx);
		buxn_jit_reg_t srsp_reg = buxn_jit_alloc_reg(ctx);

		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			swsp_reg, 0,
			SLJIT_S(BUXN_JIT_S_WSP), 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			srsp_reg, 0,
			SLJIT_S(BUXN_JIT_S_RSP), 0
		);
		ctx->wsp_reg = swsp_reg;
		ctx->rsp_reg = srsp_reg;
#if BUXN_JIT_VERBOSE
		fprintf(stderr, "  ; }}}\n");
#endif
	} else {
		ctx->ewsp = &ctx->wsp;
		ctx->ersp = &ctx->rsp;

		ctx->wsp_reg = SLJIT_S(BUXN_JIT_S_WSP);
		ctx->rsp_reg = SLJIT_S(BUXN_JIT_S_RSP);
	}

	switch (ctx->current_opcode) {
		BUXN_OPCODE_DISPATCH(BUXN_JIT_DISPATCH)
	}
}
