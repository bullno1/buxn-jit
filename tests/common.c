#include <buxn/vm/vm.h>
#include <buxn/vm/jit.h>
#include <barena.h>

void
buxn_vm_deo(buxn_vm_t* vm, uint8_t address) {
}

uint8_t
buxn_vm_dei(buxn_vm_t* vm, uint8_t address) {
	return vm->device[address];
}

void*
buxn_jit_alloc(buxn_jit_alloc_ctx_t* ctx, size_t size, size_t alignment) {
	return barena_memalign((barena_t*)ctx, size, alignment);
}
