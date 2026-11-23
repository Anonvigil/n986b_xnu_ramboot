/* SPDX-License-Identifier: BSD-2-Clause */
.text
/* AArch64 Linux boot ABI: x0=physical FDT, x1=x2=x3=0, MMU off.
 * Entry PA, FDT PA, six [start,end) cache ranges arrive in x0,x1,x2.
 * This function is stackless and assumes verified identity mappings at EL1.
 * It is compiled only into the separate branch candidate UEFI app. */
.global B2RLinuxHandoff
.type B2RLinuxHandoff, %function
.p2align 2
B2RLinuxHandoff:
    mov x26, x0
    mov x27, x1
    mov x28, x2
    mov x29, #6
    msr daifset, #0xf
    dsb sy
.Lnext_range:
    ldp x0, x1, [x28], #16
    bl .Lclean_range
    subs x29, x29, #1
    b.ne .Lnext_range
    mrs x1, sctlr_el1
    bic x1, x1, #1
    bic x1, x1, #4
    bic x1, x1, #0x1000
    dsb sy
    msr sctlr_el1, x1
    isb
    tlbi vmalle1
    dsb sy
    isb
    mov x0, x27
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    br x26

/* Clean data to PoC and invalidate I-cache to PoU by VA. Based on the
 * already exercised N986B XnuHandoff.S, without trapped cache geometry. */
.Lclean_range:
    mov x8, x0
    mrs x2, ctr_el0
    ubfx x3, x2, #16, #4
    mov x4, #4
    lsl x3, x4, x3
    sub x5, x3, #1
    bic x0, x0, x5
.Ldc_line:
    dc cvac, x0
    add x0, x0, x3
    cmp x0, x1
    b.lo .Ldc_line
    dsb sy
    and x3, x2, #0xf
    lsl x3, x4, x3
    sub x5, x3, #1
    bic x0, x8, x5
.Lic_line:
    ic ivau, x0
    add x0, x0, x3
    cmp x0, x1
    b.lo .Lic_line
    dsb sy
    isb
    ret
