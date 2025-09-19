add_library(buxn-jit STATIC "jit.c")
target_include_directories(buxn-jit PUBLIC "../include")
target_link_libraries(buxn-jit PRIVATE buxn sljit)

if (LINUX OR BSD)
	add_library(buxn-jit-gdb-hook STATIC "gdb/hook.c")
	target_include_directories(buxn-jit-gdb-hook PUBLIC "../include")
	target_link_libraries(buxn-jit-gdb-hook PRIVATE buxn)
	# When using mold linker with `--separate-debug-file`, gdb will not be able to
	# locate debug symbol for this global variable even with visibility("default")
	# However, if the symbol is exported, it will be able to.
	target_link_options(
		buxn-jit-gdb-hook PUBLIC
		-Wl,--export-dynamic-symbol=__jit_debug_register_code
	)
endif ()

if (LINUX)
	add_library(buxn-jit-perf-hook STATIC "perf.c")
	target_include_directories(buxn-jit-perf-hook PUBLIC "../include")
	target_link_libraries(buxn-jit-perf-hook PRIVATE buxn)
endif ()

if (LINUX OR BSD)
	add_library(buxn-jit-gdb-reader SHARED "gdb/reader.c")
	target_include_directories(buxn-jit-gdb-reader PUBLIC "../include")
	target_link_libraries(buxn-jit-gdb-reader PRIVATE blibs)
endif ()

add_executable(buxn-jit-cli "cli.c")
target_link_libraries(buxn-jit-cli PRIVATE
	buxn-jit
	buxn-devices
	buxn-vm
	buxn-dbg-symtab
	blibs
)

if (LINUX)
	target_link_libraries(buxn-jit-cli PRIVATE buxn-jit-gdb-hook buxn-jit-perf-hook)
elseif (BSD)
	target_link_libraries(buxn-jit-cli PRIVATE buxn-jit-gdb-hook stdthreads)
elseif (WIN32)
	set_property(TARGET buxn-jit-cli PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
