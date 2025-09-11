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

static btest_suite_t optimization = {
	.name = "optimization",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(optimization, add) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#01 #01 ADD"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x02);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}

BTEST(optimization, inc) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#ff INC ?{ #01 }"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x01);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}

BTEST(optimization, INCk) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#01 INCk"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 2);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x01);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[1], 0x02);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}

BTEST(optimization, ORAk) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#0102 ORAk"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 3);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x01);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[1], 0x02);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[2], 0x03);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}

BTEST(optimization, ORAk_2) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"#01 #02 ORAk"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 3);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0x01);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[1], 0x02);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[2], 0x03);

	buxn_jit_stats_t* stats = buxn_jit_stats(fixture.jit);
	BTEST_EXPECT_EQUAL("%d", stats->num_bounces, 0);
}
