/* SPDX-License-Identifier: BSD-2-Clause */
#include "Xnu8796Layout.h"
#include "Arena.h"

#define B2L_EFI_LOADER_DATA 2U
#define B2L_KERNEL_STREAM_MAX 0x8000000ULL
#define U64_MAX_VALUE ((b2l_u64)~(b2l_u64)0)

_Static_assert(B2L_XNU8796_INTERIOR_GUARD * 2 < N986B_MIN_ARENA,
               "the interior window must exist");
_Static_assert((B2L_XNU8796_VIRT_BASE &
                (B2L_XNU8796_INTERIOR_GUARD - 1)) == 0,
               "Scion base must be 32 MiB-aligned");
_Static_assert((N986B_RAM_START &
                (B2L_XNU8796_INTERIOR_GUARD - 1)) == 0,
               "N986B arena must be 32 MiB-aligned");
_Static_assert(sizeof(struct b2l_boot_args) == 736,
               "this planner is specific to XNU 8796 boot_args");

static int end_of(b2l_u64 base, b2l_u64 size, b2l_u64 *end)
{
    if (!size || base > U64_MAX_VALUE - size) return 0;
    *end = base + size;
    return 1;
}

static int align_up(b2l_u64 value, b2l_u64 align, b2l_u64 *aligned)
{
    if (!align || (align & (align - 1)) || value > U64_MAX_VALUE - (align - 1))
        return 0;
    *aligned = (value + align - 1) & ~(align - 1);
    return 1;
}

static int outside_arena(const struct b2l_xnu8796_region *region,
                         b2l_u64 arena_base, b2l_u64 arena_end)
{
    b2l_u64 region_end;
    return end_of(region->base, region->size, &region_end) &&
           (region_end <= arena_base || region->base >= arena_end);
}

int b2l_xnu8796_plan(const struct b2l_xnu8796_request *request,
                     struct b2l_xnu8796_layout *layout)
{
    struct b2l_macho mach = {0};
    struct b2l_xnu8796_layout result = {0};
    const struct b2l_xnu8796_region *live[6];
    b2l_u64 arena_end, first_segment, header_offset, remaining, cursor;
    b2l_u64 ramdisk_end, device_tree_end, boot_args_end;

    if (!request || !layout || !request->kernel_file || !request->kernel_file_size ||
        request->kernel_file_size > B2L_KERNEL_STREAM_MAX ||
        !request->efi_map || !request->efi_map_bytes ||
        request->efi_descriptor_stride < 40 ||
        request->arena_base != N986B_RAM_START ||
        request->arena_size != N986B_MIN_ARENA ||
        (request->ramdisk_size &&
         (request->ramdisk_size < B2L_XNU8796_ROOT_ALIGN ||
          request->ramdisk_size > B2L_XNU8796_ROOT_MAX ||
          (request->ramdisk_size & (B2L_XNU8796_ROOT_ALIGN - 1)))))
        return B2L_XNU8796_PLAN_INPUT;

    arena_end = request->arena_base + request->arena_size; /* fixed, no wrap */
    if (n986b_validate_arena(request->efi_map, request->efi_map_bytes,
                             request->efi_descriptor_stride,
                             request->arena_base, request->arena_size,
                             B2L_EFI_LOADER_DATA) != 0)
        return B2L_XNU8796_PLAN_EFI_OWNER;

    live[0] = &request->live.efi_image;
    live[1] = &request->live.efi_stack;
    live[2] = &request->live.live_fdt;
    live[3] = &request->live.usb_stream;
    live[4] = &request->live.upload_snapshot;
    live[5] = &request->live.framebuffer;
    for (unsigned i = 0; i < 6; ++i)
        if (!outside_arena(live[i], request->arena_base, arena_end))
            return B2L_XNU8796_PLAN_LIVE_OVERLAP;

    if (b2l_parse_macho(request->kernel_file, request->kernel_file_size,
                        &mach) != 0)
        return B2L_XNU8796_PLAN_MACHO;

    result.virtual_base = B2L_XNU8796_VIRT_BASE;
    result.physical_base = request->arena_base + B2L_XNU8796_INTERIOR_GUARD;
    result.memory_size = request->arena_size - 2 * B2L_XNU8796_INTERIOR_GUARD;
    result.published_end = result.physical_base + result.memory_size;

    /* The actual compiled image has its Mach header in __TEXT at +0x4000
     * from the 2 MiB-rounded parsed base, itself 48 MiB after virtBase. */
    if (mach.segment_count == 0 ||
        mach.virtual_base != result.virtual_base + B2L_XNU8796_LINK_OFFSET ||
        mach.segments[0].file_offset != 0 ||
        mach.segments[0].va != mach.virtual_base + 0x4000 ||
        mach.virtual_end < mach.virtual_base ||
        mach.entry < mach.virtual_base || mach.entry >= mach.virtual_end ||
        mach.virtual_end - result.virtual_base > result.memory_size)
        return B2L_XNU8796_PLAN_GEOMETRY;

    first_segment = mach.segments[0].va;
    result.kernel_base_pa = result.physical_base + B2L_XNU8796_LINK_OFFSET;
    result.mach_header_pa = result.physical_base +
                            first_segment - result.virtual_base;
    result.kernel_end_pa = result.physical_base +
                           mach.virtual_end - result.virtual_base;
    result.entry_pa = result.physical_base + mach.entry - result.virtual_base;
    if (result.mach_header_pa < result.kernel_base_pa ||
        result.entry_pa < result.mach_header_pa ||
        result.entry_pa >= result.kernel_end_pa ||
        result.kernel_end_pa >= result.published_end)
        return B2L_XNU8796_PLAN_GEOMETRY;

    /* Check every actual compiled segment in the physical publication window.
     * MachO.c has already bounded its file source and rounded its VM size. */
    for (unsigned i = 0; i < mach.segment_count; ++i) {
        const struct b2l_segment *segment = &mach.segments[i];
        b2l_u64 physical, physical_end;
        if (segment->va < mach.virtual_base ||
            segment->va > mach.virtual_end ||
            (segment->va & (B2L_XNU8796_PAGE_SIZE - 1)) ||
            (segment->size & (B2L_XNU8796_PAGE_SIZE - 1)) ||
            segment->size > mach.virtual_end - segment->va)
            return B2L_XNU8796_PLAN_GEOMETRY;
        physical = result.physical_base +
                   (segment->va - result.virtual_base);
        if (!end_of(physical, segment->size, &physical_end) ||
            physical < result.mach_header_pa ||
            physical_end > result.kernel_end_pa ||
            physical_end > result.published_end)
            return B2L_XNU8796_PLAN_GEOMETRY;
    }

    /* start.s derives its first V=P and KVA table coverage by subtracting
     * the Mach-header offset from memSize, then truncating to L2 blocks. */
    header_offset = result.mach_header_pa - result.physical_base;
    if (header_offset >= result.memory_size)
        return B2L_XNU8796_PLAN_GEOMETRY;
    remaining = result.memory_size - header_offset;
    result.bootstrap_map_end =
        (result.mach_header_pa & ~(B2L_XNU8796_L2_BLOCK - 1)) +
        (remaining / B2L_XNU8796_L2_BLOCK) * B2L_XNU8796_L2_BLOCK;
    if (result.bootstrap_map_end > result.published_end ||
        result.bootstrap_map_end < result.kernel_end_pa)
        return B2L_XNU8796_PLAN_CAPACITY;

    if (!align_up(result.kernel_end_pa, B2L_XNU8796_ROOT_ALIGN, &cursor))
        return B2L_XNU8796_PLAN_CAPACITY;
    if (request->ramdisk_size) {
        result.ramdisk_pa = cursor;
        result.ramdisk_size = request->ramdisk_size;
        if (!end_of(cursor, request->ramdisk_size, &cursor))
            return B2L_XNU8796_PLAN_CAPACITY;
    }
    ramdisk_end = cursor;
    if (!align_up(cursor, B2L_XNU8796_ROOT_ALIGN, &result.device_tree_pa) ||
        !end_of(result.device_tree_pa, B2L_XNU8796_DT_CAPACITY,
                &device_tree_end) ||
        !align_up(device_tree_end, B2L_XNU8796_ROOT_ALIGN,
                  &result.boot_args_pa) ||
        !end_of(result.boot_args_pa, sizeof(struct b2l_boot_args),
                &boot_args_end) ||
        !align_up(boot_args_end, B2L_XNU8796_ROOT_ALIGN,
                  &result.top_of_kernel_data))
        return B2L_XNU8796_PLAN_CAPACITY;
    result.device_tree_capacity = B2L_XNU8796_DT_CAPACITY;

    if ((result.physical_base & (B2L_XNU8796_INTERIOR_GUARD - 1)) ||
        (result.virtual_base & (B2L_XNU8796_INTERIOR_GUARD - 1)) ||
        (request->ramdisk_size &&
         result.ramdisk_pa < result.kernel_end_pa) ||
        ramdisk_end > result.device_tree_pa ||
        device_tree_end > result.boot_args_pa ||
        boot_args_end > result.top_of_kernel_data ||
        result.top_of_kernel_data > result.bootstrap_map_end ||
        result.top_of_kernel_data >
            result.published_end - B2L_XNU8796_FREE_HEADROOM)
        return B2L_XNU8796_PLAN_CAPACITY;

    *layout = result;
    return B2L_XNU8796_PLAN_OK;
}

int b2l_xnu8796_segment_physical(const struct b2l_xnu8796_layout *layout,
                                 const struct b2l_segment *segment,
                                 b2l_u64 *physical)
{
    b2l_u64 offset, end;
    if (!layout || !segment || !physical || !segment->size ||
        segment->va < layout->virtual_base ||
        layout->kernel_end_pa <= layout->physical_base)
        return B2L_XNU8796_PLAN_INPUT;
    offset = segment->va - layout->virtual_base;
    if (offset > U64_MAX_VALUE - layout->physical_base)
        return B2L_XNU8796_PLAN_GEOMETRY;
    if (layout->physical_base + offset < layout->mach_header_pa ||
        !end_of(layout->physical_base + offset, segment->size, &end) ||
        end > layout->kernel_end_pa ||
        (layout->ramdisk_pa && end > layout->ramdisk_pa) ||
        end > layout->device_tree_pa)
        return B2L_XNU8796_PLAN_GEOMETRY;
    *physical = layout->physical_base + offset;
    return B2L_XNU8796_PLAN_OK;
}
