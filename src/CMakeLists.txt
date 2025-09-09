add_library(buxn-jit STATIC "jit.c")
target_include_directories(buxn-jit PUBLIC "../include")
target_link_libraries(buxn-jit PRIVATE buxn sljit)

add_executable(buxn-jit-cli "cli.c")
target_link_libraries(buxn-jit-cli PRIVATE buxn-jit buxn-devices buxn-vm blibs)
