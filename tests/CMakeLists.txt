set(BUXN_JIT_TEST_SOURCES
	"main.c"
	"common.c"

	"basic.c"
	"memory.c"
	"device.c"
	"jump.c"
	"opctest.c"
	"optimization.c"
)

add_executable(buxn-jit-tests ${BUXN_JIT_TEST_SOURCES})
target_link_libraries(buxn-jit-tests PRIVATE
	buxn-jit
	buxn-vm
	buxn-asm
	buxn-devices
	blibs
)
