/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef N986B_XNU_ARENA_H
#define N986B_XNU_ARENA_H

#include "MachO.h"

/* First boot uses only the two adjacent 512 MiB conventional banks confirmed
 * at 0x900000000..0x940000000 in the N986B live FDT and EFI map. */
#define N986B_RAM_START     0x900000000ULL
#define N986B_RAM_END       0x940000000ULL
#define N986B_ARENA_ALIGN   0x200000ULL
#define N986B_MIN_ARENA     0x40000000ULL
#define N986B_MAX_ARENA     0x40000000ULL
#define N986B_EFI_HEADROOM  0ULL
#define N986B_MAX_CHUNKS    32U

/* The XNU virtual base is +16 MiB modulo a 32 MiB L2 block. Keep the
 * complete 1 GiB EFI allocation, but publish only its congruent suffix. */
#define N986B_XNU_L2_BLOCK   0x2000000ULL
#define N986B_XNU_PAGE_SIZE  0x4000ULL
#define N986B_XNU_LOAD_OFFSET 0x1000000ULL
/* The 16 KiB-granule bootstrap maps complete 32 MiB L2 blocks. Leave the
 * final 16 MiB of the EFI-owned arena unpublished until partial-block
 * mappings are implemented. */
#define N986B_XNU_UNMAPPED_TAIL 0x1000000ULL
#define N986B_XNU_PHYS_BASE  (N986B_RAM_START + N986B_XNU_LOAD_OFFSET)
#define N986B_XNU_PHYS_SIZE  (N986B_MIN_ARENA - N986B_XNU_LOAD_OFFSET - N986B_XNU_UNMAPPED_TAIL)

struct n986b_arena_chunk {
  b2l_u64 base;
  b2l_u64 size;
};

/* EFI descriptors need not be sorted or coalesced. The chooser can cross
 * adjacent conventional descriptors, including N986B's 512 MiB banks. */
int n986b_choose_arena(const void *map, b2l_size bytes, b2l_size stride,
                       b2l_u64 *base, b2l_u64 *size);
int n986b_validate_arena(const void *map, b2l_size bytes, b2l_size stride,
                         b2l_u64 base, b2l_u64 size, b2l_u32 type);
int n986b_arena_chunks(const void *map, b2l_size bytes, b2l_size stride,
                       b2l_u64 base, b2l_u64 size,
                       struct n986b_arena_chunk chunks[N986B_MAX_CHUNKS],
                       unsigned *count);

#endif
