# --- buxn ---

add_library(buxn INTERFACE)
target_include_directories(buxn INTERFACE "buxn/include")
set_target_properties(buxn PROPERTIES FOLDER "deps/buxn")

add_library(buxn-vm STATIC "buxn/src/vm/vm.c")
target_link_libraries(buxn-vm PUBLIC buxn)
set_target_properties(buxn-vm PROPERTIES FOLDER "deps/buxn")

add_library(buxn-asm STATIC "buxn/src/asm/asm.c")
target_link_libraries(buxn-asm PUBLIC buxn)
set_target_properties(buxn-asm PROPERTIES FOLDER "deps/buxn")

add_library(buxn-dbg-symtab STATIC "buxn/src/dbg/symtab.c")
target_link_libraries(buxn-dbg-symtab PUBLIC buxn blibs)
set_target_properties(buxn-dbg-symtab PROPERTIES FOLDER "deps/buxn")

add_library(buxn-devices STATIC
	"buxn/src/devices/console.c"
	"buxn/src/devices/system.c"
	"buxn/src/devices/datetime.c"
)
target_link_libraries(buxn-devices PUBLIC buxn)
set_target_properties(buxn-devices PROPERTIES FOLDER "deps/buxn")

# --- sljit ---

add_library(sljit STATIC "sljit/sljit_src/sljitLir.c")
target_include_directories(sljit PUBLIC "sljit/sljit_src")
set_target_properties(sljit PROPERTIES FOLDER "deps")

# --- blibs ---

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")
set_target_properties(blibs PROPERTIES FOLDER "deps")
