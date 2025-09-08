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

static btest_suite_t memory = {
	.name = "memory",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(memory, lit) {
	fixture.vm->memory[BUXN_RESET_VECTOR + 0] = 0x80; // LIT
	fixture.vm->memory[BUXN_RESET_VECTOR + 1] = 0x42;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 1);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 0x42);
}

BTEST(memory, lit2) {
	fixture.vm->memory[BUXN_RESET_VECTOR + 0] = 0xa0; // LIT2
	fixture.vm->memory[BUXN_RESET_VECTOR + 1] = 0x42;
	fixture.vm->memory[BUXN_RESET_VECTOR + 2] = 0x69;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_ASSERT_EQUAL("%d", fixture.vm->wsp, 2);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[0], 0x42);
	BTEST_ASSERT_EQUAL("%d", fixture.vm->ws[1], 0x69);
}
