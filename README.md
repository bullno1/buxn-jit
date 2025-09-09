# buxn-jit

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A JIT runtime for [buxn](https://github.com/bullno1/buxn).

## API

Refer to [jit.h](include/buxn/vm/jit.h).

`buxn_jit_execute` is a drop-in replacement for `buxn_vm_execute`.
Execution of complex vectors would be automatically faster.
