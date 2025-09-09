#include <btest.h>
#include <barena.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>
#include "common.h"

static struct {
	barena_pool_t pool;
	barena_t arena;
	buxn_jit_t* jit;
	buxn_vm_t* vm;
} fixture;

static void
init_per_suite(void) {
	barena_pool_init(&fixture.pool, 1);
}

static void
cleanup_per_suite(void) {
	barena_pool_cleanup(&fixture.pool);
}

static void
init_per_test(void) {
	barena_init(&fixture.arena, &fixture.pool);
	fixture.vm = barena_memalign(
		&fixture.arena,
		sizeof(buxn_vm_t) + BUXN_MEMORY_BANK_SIZE,
		_Alignof(buxn_vm_t)
	);
	fixture.vm->config = (buxn_vm_config_t){
		.memory_size = BUXN_MEMORY_BANK_SIZE,
	};
	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);

	fixture.jit = buxn_jit_init(fixture.vm, (buxn_jit_alloc_ctx_t*)&fixture.arena);
}

static void
cleanup_per_test(void) {
	buxn_jit_cleanup(fixture.jit);
	barena_reset(&fixture.arena);
}

static btest_suite_t jump = {
	.name = "jump",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(jump, jmp) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		",&skip-rel JMP BRK &skip-rel #01  ( 01 )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);
}

BTEST(jump, jcn_true) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#abcd #01 ,&pass JCN SWP &pass POP  ( ab )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0xab);
}

BTEST(jump, jcn_false) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#abcd #00 ,&fail JCN SWP &fail POP  ( cd )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0xcd);
}

BTEST(jump, jsr) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		",&routine JSR                     ( | PC* )\n"
		"&routine ,&get JSR #01 BRK &get #02 JMP2r  ( 02 01 )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 2);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x02);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[1], 0x01);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->rsp, 2);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->rs[0], 0x01);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->rs[1], 0x03);
}

BTEST(jump, jci_true) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#0a #01 ?{ INC }          ( 0a )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x0a);
}

BTEST(jump, jci_false) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#0a #00 ?{ INC }          ( 0b )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x0b);
}

BTEST(jump, jmi) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#0a !{ INC }              ( 0a )"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x0a);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_blocks, 2);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}

BTEST(jump, jsi) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#07 #04 modulo BRK        ( 03 )\n"
		"@modulo ( a mod -- res )\n"
		"DIVk MUL SUB JMP2r"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x03);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->rsp, 0);
}

BTEST(jump, redirect) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"[ LIT2 =first ] JMP2 |0200 @first #01 |0300 @second #02"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x01);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_blocks, 2);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);

	// Rewrite jump target
	fixture.vm->memory[0x0101] = 0x03;
	fixture.vm->wsp = 0;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x02);

	BTEST_EXPECT_EQUAL("%d", stats->num_blocks, 3);  // New block
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 1);  // Jump trampolined
}
