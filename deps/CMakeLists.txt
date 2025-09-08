# --- buxn ---

add_library(buxn INTERFACE)
target_include_directories(buxn INTERFACE "buxn/include")

add_library(buxn-vm STATIC "buxn/src/vm/vm.c")
target_link_libraries(buxn-vm PUBLIC buxn)

# --- sljit ---

add_library(sljit STATIC "sljit/sljit_src/sljitLir.c")
target_include_directories(sljit PUBLIC "sljit/sljit_src")

# --- blibs ---

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")
