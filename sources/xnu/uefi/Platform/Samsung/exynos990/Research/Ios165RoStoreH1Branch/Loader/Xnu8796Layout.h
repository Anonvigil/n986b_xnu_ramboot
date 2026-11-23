/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef N986B_XNU8796_LAYOUT_H
#define N986B_XNU8796_LAYOUT_H

#include "BootArgs.h"

/* Pure-C preflight for the EXYNOS990 compile-only XNU 8796 Mach-O. This is
 * intentionally separate from N986BXnuLoader.c; it cannot enable handoff. */
#define B2L_XNU8796_VIRT_BASE       0xFFFFFFF004000000ULL
#define B2L_XNU8796_LINK_OFFSET     0x03000000ULL
#define B2L_XNU8796_INTERIOR_GUARD  0x02000000ULL
#define B2L_XNU8796_PAGE_SIZE       0x1000ULL
#define B2L_XNU8796_L2_BLOCK        0x200000ULL
#define B2L_XNU8796_ROOT_ALIGN      0x4000ULL
#define B2L_XNU8796_ROOT_MAX        0x4000000ULL
#define B2L_XNU8796_DT_CAPACITY     0x10000ULL
#define B2L_XNU8796_FREE_HEADROOM   0x4000000ULL

struct b2l_xnu8796_region {
    b2l_u64 base;
    b2l_u64 size;
};

/* Every live region must be supplied with its current physical range, even
 * when a region is expected outside the high arena. The EFI map must be a
 * post-AllocatePages map showing the full 1 GiB arena as EfiLoaderData. */
struct b2l_xnu8796_live_regions {
    struct b2l_xnu8796_region efi_image;
    struct b2l_xnu8796_region efi_stack;
    struct b2l_xnu8796_region live_fdt;
    struct b2l_xnu8796_region usb_stream;
    struct b2l_xnu8796_region upload_snapshot;
    struct b2l_xnu8796_region framebuffer;
};

struct b2l_xnu8796_request {
    const void *kernel_file;
    b2l_size kernel_file_size;
    const void *efi_map;
    b2l_size efi_map_bytes;
    b2l_size efi_descriptor_stride;
    b2l_u64 arena_base;
    b2l_u64 arena_size;
    b2l_u64 ramdisk_size; /* 0 or a 16 KiB multiple, at most 64 MiB */
    struct b2l_xnu8796_live_regions live;
};

struct b2l_xnu8796_layout {
    b2l_u64 virtual_base;
    b2l_u64 physical_base;
    b2l_u64 memory_size;
    b2l_u64 published_end;
    b2l_u64 kernel_base_pa; /* 2 MiB-rounded parsed segment base */
    b2l_u64 mach_header_pa; /* first segment at file offset zero */
    b2l_u64 kernel_end_pa;
    b2l_u64 entry_pa;
    b2l_u64 ramdisk_pa; /* zero if no ramdisk */
    b2l_u64 ramdisk_size;
    b2l_u64 device_tree_pa;
    b2l_u64 device_tree_capacity;
    b2l_u64 boot_args_pa;
    b2l_u64 top_of_kernel_data;
    b2l_u64 bootstrap_map_end;
};

enum b2l_xnu8796_plan_result {
    B2L_XNU8796_PLAN_OK = 0,
    B2L_XNU8796_PLAN_INPUT = -1,
    B2L_XNU8796_PLAN_MACHO = -2,
    B2L_XNU8796_PLAN_EFI_OWNER = -3,
    B2L_XNU8796_PLAN_LIVE_OVERLAP = -4,
    B2L_XNU8796_PLAN_GEOMETRY = -5,
    B2L_XNU8796_PLAN_CAPACITY = -6
};

int b2l_xnu8796_plan(const struct b2l_xnu8796_request *request,
                     struct b2l_xnu8796_layout *layout);
/* Recheck a parsed Mach-O segment against the planned physical window before
 * runtime copy. This is deliberately shared by the host replay and loader. */
int b2l_xnu8796_segment_physical(const struct b2l_xnu8796_layout *layout,
                                 const struct b2l_segment *segment,
                                 b2l_u64 *physical);

#endif
