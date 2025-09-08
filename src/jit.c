// vim: set foldmethod=marker foldlevel=0:
#include <buxn/vm/jit.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/opcodes.h>
#include <sljitLir.h>
#include <stdbool.h>
#include <string.h>
#define BHAMT_HASH_TYPE uint32_t
#include "hamt.h"

#define BUXN_JIT_ADDR_EQ(LHS, RHS) (LHS == RHS)
#define BUXN_JIT_MEM() SLJIT_MEM2(SLJIT_R(BUXN_JIT_R_MEM_BASE), SLJIT_R(BUXN_JIT_R_MEM_OFFSET))

#define BUXN_JIT_OP_K 0x80
#define BUXN_JIT_OP_R 0x40
#define BUXN_JIT_OP_2 0x20

enum {
	BUXN_JIT_S_VM = 0,

	BUXN_JIT_S_COUNT,
};

enum {
	BUXN_JIT_R_MEM_BASE = 0,
	BUXN_JIT_R_MEM_OFFSET,
	BUXN_JIT_R_WSP,
	BUXN_JIT_R_RSP,
	BUXN_JIT_R_SWSP,
	BUXN_JIT_R_SRSP,
	BUXN_JIT_R_OP_A,
	BUXN_JIT_R_OP_B,
	BUXN_JIT_R_OP_C,
	BUXN_JIT_R_OP_T,

	BUXN_JIT_R_COUNT,
};

#undef BUXN_OPCODE_NAME
#define BUXN_OPCODE_NAME(NAME, K, R, S) NAME
#define BUXN_JIT_DISPATCH(NAME, VALUE) \
	case VALUE: BUXN_CONCAT(buxn_jit_, NAME)(&ctx); break;

typedef sljit_u32 (*buxn_jit_fn_t)(sljit_up vm);

typedef struct buxn_jit_block_s buxn_jit_block_t;
struct buxn_jit_block_s {
	uint16_t key;
	buxn_jit_block_t* children[BHAMT_NUM_CHILDREN];

	buxn_jit_fn_t fn;
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

struct buxn_jit_s {
	buxn_vm_t* vm;
	buxn_jit_alloc_ctx_t* alloc_ctx;

	buxn_jit_block_map_t blocks;
};

typedef struct {
	buxn_jit_t* jit;
	buxn_jit_block_t* block;
	struct sljit_compiler* compiler;
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

static void
buxn_jit(buxn_jit_t* jit, uint16_t pc, buxn_jit_block_t* block);

buxn_jit_t*
buxn_jit_init(buxn_vm_t* vm, buxn_jit_alloc_ctx_t* alloc_ctx) {
	buxn_jit_t* jit = buxn_jit_alloc(alloc_ctx, sizeof(buxn_jit_t), _Alignof(buxn_jit_t));
	*jit = (buxn_jit_t){
		.vm = vm,
		.alloc_ctx = alloc_ctx,
	};
	return jit;
}

void
buxn_jit_execute(buxn_jit_t* jit, uint16_t pc) {
	while (pc != 0) {
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

			buxn_jit(jit, pc, block);
		}

		if (block->fn != NULL) {
			pc = (uint16_t)block->fn((uintptr_t)jit->vm);
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

// Micro ops {{{

static inline bool
buxn_jit_op_flag_2(buxn_jit_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_JIT_OP_2;
}

static inline bool
buxn_jit_op_flag_k(buxn_jit_ctx_t* ctx) {
	return ctx->current_opcode & BUXN_JIT_OP_K;
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

static buxn_jit_operand_t
buxn_jit_pop_ex(buxn_jit_ctx_t* ctx, buxn_jit_reg_t reg, bool flag_2, bool flag_r) {
	buxn_jit_value_t* stack = flag_r ? ctx->rst : ctx->wst;
	uint8_t* stack_ptr = flag_r ? ctx->ersp : ctx->ewsp;

	sljit_sw mem_base = flag_r ? SLJIT_OFFSETOF(buxn_vm_t, rs) : SLJIT_OFFSETOF(buxn_vm_t, ws);
	buxn_jit_reg_t stack_ptr_reg = flag_r ? ctx->rsp_reg : ctx->wsp_reg;

	buxn_jit_operand_t operand = { .is_short = flag_2, .reg = reg };

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
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			reg, 0,
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
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			SLJIT_R(BUXN_JIT_R_OP_T), 0,
			BUXN_JIT_MEM(), 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_SHL,
			SLJIT_R(BUXN_JIT_R_OP_T), 0,
			SLJIT_R(BUXN_JIT_R_OP_T), 0,
			SLJIT_IMM, 8
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_OR,
			reg, 0,
			reg, 0,
			SLJIT_R(BUXN_JIT_R_OP_T), 0
		);
	} else {
		buxn_jit_value_t value = stack[--(*stack_ptr)];
		operand.const_value = value.const_value;
		operand.semantics = value.semantics;

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
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			reg, 0,
			BUXN_JIT_MEM(), 0
		);
	}
	return operand;
}

static void
buxn_jit_push_ex(buxn_jit_ctx_t* ctx, buxn_jit_operand_t operand, bool flag_r) {
	buxn_jit_value_t* stack = flag_r ? ctx->rst : ctx->wst;
	uint8_t* stack_ptr = flag_r ? ctx->ersp : ctx->ewsp;

	sljit_sw mem_base = flag_r ? SLJIT_OFFSETOF(buxn_vm_t, rs) : SLJIT_OFFSETOF(buxn_vm_t, ws);
	buxn_jit_reg_t stack_ptr_reg = flag_r ? ctx->rsp_reg : ctx->wsp_reg;

	if (operand.is_short) {
		buxn_jit_value_t* hi = &stack[(*stack_ptr)++];
		buxn_jit_value_t* lo = &stack[(*stack_ptr)++];
		hi->semantics = lo->semantics = operand.semantics;
		hi->const_value = operand.const_value >> 8;
		lo->const_value = operand.const_value & 0xff;

		buxn_jit_set_mem_base(ctx, mem_base);

		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_LSHR,
			SLJIT_R(BUXN_JIT_R_OP_T), 0,
			operand.reg, 0,
			SLJIT_IMM, 8
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			SLJIT_R(BUXN_JIT_R_OP_T), 0
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
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
			stack_ptr_reg, 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_AND,
			SLJIT_R(BUXN_JIT_R_OP_T), 0,
			operand.reg, 0,
			SLJIT_IMM, 0xff
		);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			BUXN_JIT_MEM(), 0,
			SLJIT_R(BUXN_JIT_R_OP_T), 0
		);
		sljit_emit_op2(
			ctx->compiler,
			SLJIT_ADD,
			stack_ptr_reg, 0,
			stack_ptr_reg, 0,
			SLJIT_IMM, 1
		);
	} else {
		buxn_jit_value_t* value = &stack[(*stack_ptr)++];
		value->semantics = operand.semantics;
		value->const_value = (uint8_t)operand.const_value;

		buxn_jit_set_mem_base(ctx, mem_base);
		sljit_emit_op1(
			ctx->compiler,
			SLJIT_MOV_U8,
			SLJIT_R(BUXN_JIT_R_MEM_OFFSET), 0,
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

static buxn_jit_operand_t
buxn_jit_pop(buxn_jit_ctx_t* ctx, buxn_jit_reg_t reg) {
	return buxn_jit_pop_ex(ctx, reg, buxn_jit_op_flag_2(ctx), buxn_jit_op_flag_r(ctx));
}

static void
buxn_jit_push(buxn_jit_ctx_t* ctx, buxn_jit_operand_t operand) {
	buxn_jit_push_ex(ctx, operand, buxn_jit_op_flag_r(ctx));
}

// }}}

// Opcodes {{{

static void
buxn_jit_BRK(buxn_jit_ctx_t* ctx) {
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, wsp),
		SLJIT_R(BUXN_JIT_R_WSP), 0
	);
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV_U8,
		SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, rsp),
		SLJIT_R(BUXN_JIT_R_RSP), 0
	);
	sljit_emit_return(ctx->compiler, SLJIT_MOV32, SLJIT_IMM, 0);
	ctx->block->fn = (buxn_jit_fn_t)sljit_generate_code(ctx->compiler, 0, NULL);
	sljit_free_compiler(ctx->compiler);
	ctx->compiler = NULL;
}

static void
buxn_jit_INC(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t operand = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
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
	buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
}

static void
buxn_jit_NIP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	(void)a;
	buxn_jit_push(ctx, b);
}

static void
buxn_jit_SWP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_ROT(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t c = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_C));
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, c);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_DUP(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	buxn_jit_push(ctx, a);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_OVR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	buxn_jit_push(ctx, a);
	buxn_jit_push(ctx, b);
	buxn_jit_push(ctx, a);
}

static void
buxn_jit_EQU(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? (BUXN_JIT_SEM_CONST | BUXN_JIT_SEM_BOOLEAN)
			: 0,
		.const_value = a.const_value == b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
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

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_NEQ(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? (BUXN_JIT_SEM_CONST | BUXN_JIT_SEM_BOOLEAN)
			: 0,
		.const_value = a.const_value != b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
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

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_GTH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? (BUXN_JIT_SEM_CONST | BUXN_JIT_SEM_BOOLEAN)
			: 0,
		.const_value = a.const_value > b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
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

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_LTH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? (BUXN_JIT_SEM_CONST | BUXN_JIT_SEM_BOOLEAN)
			: 0,
		.const_value = a.const_value < b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
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

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_JMP(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_JCN(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_JSR(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_STH(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));
	buxn_jit_push_ex(ctx, a, !buxn_jit_op_flag_r(ctx));
}

static void
buxn_jit_LDZ(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_STZ(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_LDR(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_STR(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_LDA(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_STA(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_DEI(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_DEO(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_ADD(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value + b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_ADD,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_SUB(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value - b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_SUB,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_MUL(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value * b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_MUL,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_DIV(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
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

	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R(BUXN_JIT_R_OP_T), 0,
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
		SLJIT_R(BUXN_JIT_R_OP_T), 0
	);
	struct sljit_jump* end = sljit_emit_jump(ctx->compiler, SLJIT_JUMP);

	sljit_set_label(set_zero, sljit_emit_label(ctx->compiler));
	sljit_emit_op1(
		ctx->compiler,
		SLJIT_MOV,
		SLJIT_R(BUXN_JIT_R_OP_C), 0,
		SLJIT_IMM, 0
	);

	sljit_set_label(end, sljit_emit_label(ctx->compiler));
	buxn_jit_push(ctx, c);
}

static void
buxn_jit_AND(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value & b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_AND,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_ORA(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value | b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_OR,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_EOR(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_B));
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = a.const_value ^ b.const_value,
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_XOR,
		c.reg, 0,
		a.reg, 0,
		b.reg, 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_SFT(buxn_jit_ctx_t* ctx) {
	buxn_jit_operand_t b = buxn_jit_pop_ex(ctx, SLJIT_R(BUXN_JIT_R_OP_B), false, buxn_jit_op_flag_r(ctx));;
	buxn_jit_operand_t a = buxn_jit_pop(ctx, SLJIT_R(BUXN_JIT_R_OP_A));

	buxn_jit_operand_t c = {
		.is_short = b.is_short,
		.semantics = ((a.semantics & BUXN_JIT_SEM_CONST) && (b.semantics & BUXN_JIT_SEM_CONST))
			? BUXN_JIT_SEM_CONST
			: 0,
		.const_value = (a.const_value >> (b.const_value & 0x0f)) << ((b.const_value & 0xf0) >> 4),
		.reg = SLJIT_R(BUXN_JIT_R_OP_C),
	};
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_AND,
		SLJIT_R(BUXN_JIT_R_OP_T), 0,
		b.reg, 0,
		SLJIT_IMM, 0x0f
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_LSHR,
		c.reg, 0,
		a.reg, 0,
		SLJIT_R(BUXN_JIT_R_OP_T), 0
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_LSHR,
		SLJIT_R(BUXN_JIT_R_OP_T), 0,
		b.reg, 0,
		SLJIT_IMM, 4
	);
	sljit_emit_op2(
		ctx->compiler,
		SLJIT_SHL,
		c.reg, 0,
		c.reg, 0,
		SLJIT_R(BUXN_JIT_R_OP_T), 0
	);

	buxn_jit_push(ctx, c);
}

static void
buxn_jit_JCI(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_JMI(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_JSI(buxn_jit_ctx_t* ctx) {
}

static void
buxn_jit_LIT(buxn_jit_ctx_t* ctx) {
}

// }}}

static void
buxn_jit(buxn_jit_t* jit, uint16_t pc, buxn_jit_block_t* block) {
	struct sljit_compiler* compiler = sljit_create_compiler(NULL);
	sljit_emit_enter(
		compiler,
		0,
		SLJIT_ARGS1(32, P),
		BUXN_JIT_R_COUNT,
		BUXN_JIT_S_COUNT,
		0
	);
	sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R(BUXN_JIT_R_WSP), 0, SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, wsp));
	sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R(BUXN_JIT_R_RSP), 0, SLJIT_MEM1(SLJIT_S(BUXN_JIT_S_VM)), SLJIT_OFFSETOF(buxn_vm_t, rsp));

	buxn_jit_ctx_t ctx = {
		.compiler = compiler,
		.jit = jit,
		.pc = pc,
		.block = block,
	};

	uint8_t shadow_wsp;
	uint8_t shadow_rsp;
	while (ctx.compiler != NULL) {
		if (ctx.pc < 256) {
			sljit_free_compiler(ctx.compiler);
			ctx.compiler = NULL;
			break;
		}

		ctx.current_opcode = jit->vm->memory[ctx.pc++];

		if (buxn_jit_op_flag_k(&ctx)) {
			shadow_wsp = ctx.wsp;
			shadow_rsp = ctx.rsp;
			ctx.ewsp = &shadow_wsp;
			ctx.ersp = &shadow_rsp;

			sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R(BUXN_JIT_R_SWSP), 0, SLJIT_R(BUXN_JIT_R_WSP), 0);
			sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R(BUXN_JIT_R_SRSP), 0, SLJIT_R(BUXN_JIT_R_RSP), 0);
			ctx.wsp_reg = BUXN_JIT_R_SWSP;
			ctx.rsp_reg = BUXN_JIT_R_SRSP;
		} else {
			ctx.ewsp = &ctx.wsp;
			ctx.ersp = &ctx.rsp;

			ctx.wsp_reg = BUXN_JIT_R_WSP;
			ctx.rsp_reg = BUXN_JIT_R_RSP;
		}

		switch (ctx.current_opcode) {
			BUXN_OPCODE_DISPATCH(BUXN_JIT_DISPATCH)
		}
	}
}
