add_library(buxn-jit STATIC "jit.c")
target_include_directories(buxn-jit PUBLIC "../include")
target_link_libraries(buxn-jit PRIVATE buxn sljit)

add_library(buxn-jit-gdb-hook STATIC "gdb/hook.c")
target_include_directories(buxn-jit-gdb-hook PUBLIC "../include")
target_link_options(buxn-jit-gdb-hook PUBLIC -Wl,--export-dynamic-symbol=__jit_debug_register_code)

add_library(buxn-jit-gdb-reader SHARED "gdb/reader.c")
target_include_directories(buxn-jit-gdb-reader PUBLIC "../include")
target_link_libraries(buxn-jit-gdb-reader PRIVATE blibs)

add_executable(buxn-jit-cli "cli.c")
target_link_libraries(buxn-jit-cli PRIVATE
	buxn-jit
	buxn-jit-gdb-hook
	buxn-devices
	buxn-vm
	blibs
)
