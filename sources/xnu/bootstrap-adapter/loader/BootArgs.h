/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BOOT_ARGS_H
#define B2L_BOOT_ARGS_H
#include "MachO.h"
struct b2l_boot_video {
    b2l_u64 base, display, row_bytes, width, height, depth;
};
/* iOS 16.5 / XNU xnu-8796.121.2 boot_args ABI. Natural AArch64 alignment.
 * The older N986B experiment used xnu-12377 and a 1024-byte command line;
 * that different structure must not be passed to XNU 8796. */
#define B2L_XNU_BOOT_LINE_LENGTH 608
struct b2l_boot_args {
    unsigned short revision, version;
    b2l_u64 virtual_base, physical_base, memory_size, top_of_kernel_data;
    struct b2l_boot_video video;
    b2l_u32 machine_type;
    b2l_u64 device_tree;
    b2l_u32 device_tree_length;
    char command_line[B2L_XNU_BOOT_LINE_LENGTH];
    b2l_u64 flags, actual_memory_size;
};
#ifdef B2L_UEFI_BUILD
STATIC_ASSERT(sizeof(struct b2l_boot_args) == 736, "XNU 8796 boot_args size");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, command_line) == 108, "XNU command line offset");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, video) == 40, "XNU Boot_Video offset");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, flags) == 720, "XNU 8796 flags offset");
STATIC_ASSERT(OFFSET_OF(struct b2l_boot_args, actual_memory_size) == 728, "XNU 8796 actual memory offset");
#else
_Static_assert(sizeof(struct b2l_boot_args) == 736, "XNU 8796 boot_args size");
_Static_assert(offsetof(struct b2l_boot_args, command_line) == 108, "XNU command line offset");
_Static_assert(offsetof(struct b2l_boot_args, video) == 40, "XNU Boot_Video offset");
_Static_assert(offsetof(struct b2l_boot_args, flags) == 720, "XNU 8796 flags offset");
_Static_assert(offsetof(struct b2l_boot_args, actual_memory_size) == 728, "XNU 8796 actual memory offset");
#endif
#endif
