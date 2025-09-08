#include <btest.h>
#include <barena.h>
#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>

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

static btest_suite_t basic = {
	.name = "basic",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(basic, empty) {
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);
}

BTEST(basic, arithmetic) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x18; // ADD
	fixture.vm->ws[0] = 1;
	fixture.vm->ws[1] = 2;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 3);
}

BTEST(basic, arithmetic_short) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x38; // ADD2
	fixture.vm->ws[0] = 0;
	fixture.vm->ws[1] = 255;
	fixture.vm->ws[2] = 0;
	fixture.vm->ws[3] = 1;
	fixture.vm->wsp = 4;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 2);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[1], 0);
}

BTEST(basic, arithmetic_keep) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x98; // ADDk
	fixture.vm->ws[0] = 1;
	fixture.vm->ws[1] = 2;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 3);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[1], 2);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[2], 3);
}

BTEST(basic, arithmetic_return) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x58; // ADDr
	fixture.vm->rs[0] = 1;
	fixture.vm->rs[1] = 2;
	fixture.vm->rsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->rsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->rs[0], 3);
}

BTEST(basic, stack_wrap_around) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x02; // POP
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 0xff);
}

BTEST(basic, stack_wrap_around_2) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x21; // INC2
	fixture.vm->ws[0] = 1;
	fixture.vm->ws[255] = 2;
	fixture.vm->wsp = 1;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 2);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[255], 2);
}

BTEST(basic, div) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x1b; // DIV
	fixture.vm->ws[0] = 6;
	fixture.vm->ws[1] = 2;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 3);

	fixture.vm->ws[0] = 6;
	fixture.vm->ws[1] = 0;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 0);
}

BTEST(basic, cmp) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x0a; // GTH
	fixture.vm->ws[0] = 6;
	fixture.vm->ws[1] = 2;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 1);
}

BTEST(basic, sft) {
	fixture.vm->memory[BUXN_RESET_VECTOR] = 0x1f; // SFT
	fixture.vm->ws[0] = 0x34;
	fixture.vm->ws[1] = 0x33;
	fixture.vm->wsp = 2;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 0x30);
}
