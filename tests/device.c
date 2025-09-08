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
	device_handler_t device_handler;

	uint16_t dei;
	uint16_t deo;
} fixture;

static uint8_t
dei(buxn_vm_t* vm, uint8_t addr, void* userdata) {
	switch (addr) {
		case 0xd2: return fixture.dei >> 8;
		case 0xd3: return fixture.dei & 0xff;
		default: return vm->device[addr];
	}
}

static void
deo(buxn_vm_t* vm, uint8_t addr, void* userdata) {
	if (addr == 0xd0) {
		fixture.deo = buxn_vm_dev_load2(vm, addr);
	}
}

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
		.userdata = &fixture.device_handler,
	};
	fixture.device_handler = (device_handler_t){
		.dei = dei,
		.deo = deo,
	};
	buxn_vm_reset(fixture.vm, BUXN_VM_RESET_ALL);

	fixture.jit = buxn_jit_init(fixture.vm, (buxn_jit_alloc_ctx_t*)&fixture.arena);
}

static void
cleanup_per_test(void) {
	buxn_jit_cleanup(fixture.jit);
	barena_reset(&fixture.arena);
}

static btest_suite_t device = {
	.name = "device",

	.init_per_suite = init_per_suite,
	.cleanup_per_suite = cleanup_per_suite,

	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

BTEST(device, dei2) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"|d0 @Test &deo $2 &dei $2 |0100 .Test/dei DEI2"
	));
	fixture.dei = 0xbeef;
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 2);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[0], 0xbe);
	BTEST_EXPECT_EQUAL("0x%02x", fixture.vm->ws[1], 0xef);
}

BTEST(device, deo2) {
	BTEST_ASSERT(buxn_asm_str(
		&fixture.arena,
		&fixture.vm->memory[BUXN_RESET_VECTOR],
		"|d0 @Test &deo $2 &dei $2 |0100 #cafe .Test/deo DEO2"
	));
	buxn_jit_execute(fixture.jit, BUXN_RESET_VECTOR);

	BTEST_EXPECT_EQUAL("%d", fixture.vm->wsp, 0);
	BTEST_EXPECT_EQUAL("0x%04x", fixture.deo, 0xcafe);
}
