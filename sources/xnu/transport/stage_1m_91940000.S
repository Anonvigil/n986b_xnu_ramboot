/* H4 paired RAM callback: 2048 bounded high 512-byte diagnostic frames,
 * then the established low FD/manifest/XNU stream. No storage I/O or retry.
 *
 * x0 = completed OUT pointer, x1 = completed byte count.
 * state @ 0x8b000200: u64 count (0..2048), u64 error (0 or BAD1).
 */
.text
.global n986b_ro_store_1m_stage
.type n986b_ro_store_1m_stage, %function
n986b_ro_store_1m_stage:
    movz x7, #0x0200
    movk x7, #0x8b00, lsl #16
    movz x4, #0x83f8
    movk x4, #0xe834, lsl #16
    cmp  x1, #0x200
    b.eq full_packet

    /* Non-full packets: installation reset or exact 8-byte terminal. */
    movz x9, #0x0000
    movk x9, #0x9194, lsl #16
    cmp  x0, x9
    b.ne maybe_install
    ldr  x8, [x7]
    cmp  x8, #2048
    b.ne reject_packet
    cmp  x1, #8
    b.ne reject_packet

    /* Keep the original, proven FD entry sequence unchanged. */
    movz x0, #0x0000
    movk x0, #0x8a00, lsl #16
    msr  daifset, #0xf
    dsb  sy
    ic   iallu
    dsb  sy
    isb
    movz x9, #0x0000
    movk x9, #0x9000, lsl #16
    br   x9

maybe_install:
    /* Only the known one-behind 8-byte install completion can reset state. */
    cmp  x1, #8
    b.ne reject_packet
    movz x9, #0x0ff8
    movk x9, #0x8b00, lsl #16
    cmp  x0, x9
    b.ne reject_packet
reset_stage:
    str  xzr, [x7]
    str  xzr, [x7, #8]
    movz x3, #0x1000
    movk x3, #0x8b00, lsl #16
    str  x3, [x4]
    ret

full_packet:
    ldr  x8, [x7]
    cmp  x8, #2048
    b.eq stream_packet
    b.hi reject_packet
    movz x9, #0x1000
    movk x9, #0x8b00, lsl #16
    cmp  x0, x9
    b.ne reject_packet

    /* Exactly one 512-byte bounce copy per count, to base + count * 512. */
    movz x2, #0x4000, lsl #16
    movk x2, #0x0009, lsl #32
    add  x2, x2, x8, lsl #9
    movz x6, #32
1:
    ldp  x3, x5, [x0], #16
    stp  x3, x5, [x2], #16
    subs x6, x6, #1
    b.ne 1b
    dsb  sy
    add  x8, x8, #1
    str  x8, [x7]
    movz x3, #0x1000
    movk x3, #0x8b00, lsl #16
    movz x9, #0x0000
    movk x9, #0x9000, lsl #16
    cmp  x8, #2048
    csel x3, x3, x9, ne
    str  x3, [x4]
    ret

stream_packet:
    movz x9, #0x0000
    movk x9, #0x9000, lsl #16
    cmp  x0, x9
    b.lo reject_packet
    movz x9, #0x0000
    movk x9, #0x9194, lsl #16
    cmp  x0, x9
    b.hs reject_packet
    add  x3, x0, x1
    cmp  x3, x9
    b.hi reject_packet
    str  x3, [x4]
    ret

reject_packet:
    movz x8, #9
    str  x8, [x7]
    movz x9, #0xbad1
    str  x9, [x7, #8]
    movz x3, #0x1000
    movk x3, #0x8b00, lsl #16
    str  x3, [x4]
    ret
.size n986b_ro_store_1m_stage, .-n986b_ro_store_1m_stage
