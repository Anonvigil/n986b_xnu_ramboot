// Host-only diagnostic entry stub for the static ARM64 PID 1.
// The current XNU test faults at _start's first stack access (SP_ALIGN).
// This stub performs no stack access before enforcing the AArch64 16-byte SP
// alignment requirement. The C diagnostic program is unchanged.

.text
.p2align 2
.globl _entry_align
_entry_align:
    mov x9, sp
    and x9, x9, #0xfffffffffffffff0
    mov sp, x9
    b _start
