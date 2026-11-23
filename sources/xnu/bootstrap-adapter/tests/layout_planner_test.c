/* Host-only tests against the three SHA-pinned EXYNOS990 compile-only builds. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Xnu8796Layout.h"
#include "../DeviceTree.h"

struct efi_descriptor {
    uint32_t type, pad;
    uint64_t base, virtual_base, pages, attributes;
};
_Static_assert(sizeof(struct efi_descriptor) == 40, "EFI map wire size");

static void set_live(struct b2l_xnu8796_live_regions *live)
{
    live->efi_image = (struct b2l_xnu8796_region){0x80000000ULL, 0x200000ULL};
    live->efi_stack = (struct b2l_xnu8796_region){0x81000000ULL, 0x10000ULL};
    live->live_fdt = (struct b2l_xnu8796_region){0x96000000ULL, 0x50000ULL};
    live->usb_stream = (struct b2l_xnu8796_region){0x91000000ULL, 0x4d00000ULL};
    live->upload_snapshot = (struct b2l_xnu8796_region){0x95d00000ULL, 0x1000ULL};
    live->framebuffer = (struct b2l_xnu8796_region){0xfa000000ULL, 0x1400000ULL};
}

static void expect_error(const struct b2l_xnu8796_request *request, int expected)
{
    struct b2l_xnu8796_layout output;
    memset(&output, 0xa5, sizeof(output));
    assert(b2l_xnu8796_plan(request, &output) == expected);
    /* The API must never return a partial layout after a failed precondition. */
    assert(output.virtual_base == 0xa5a5a5a5a5a5a5a5ULL);
}

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *kernel;
    struct efi_descriptor map[2] = {
        {2, 0, 0x900000000ULL, 0, 0x20000000ULL / 4096, 0},
        {2, 0, 0x920000000ULL, 0, 0x20000000ULL / 4096, 0},
    };
    struct b2l_xnu8796_request request = {0};
    struct b2l_xnu8796_layout output = {0};

    if (argc != 2 || !(file = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: %s EXYNOS990-kernel.macho\n", argv[0]);
        return 2;
    }
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length == 15153216L || length == 15154184L);
    assert(fseek(file, 0, SEEK_SET) == 0);
    kernel = malloc((size_t)length);
    assert(kernel && fread(kernel, 1, (size_t)length, file) == (size_t)length);
    fclose(file);

    request.kernel_file = kernel;
    request.kernel_file_size = (size_t)length;
    request.efi_map = map;
    request.efi_map_bytes = sizeof(map);
    request.efi_descriptor_stride = sizeof(map[0]);
    request.arena_base = 0x900000000ULL;
    request.arena_size = 0x40000000ULL;
    request.ramdisk_size = B2L_XNU8796_ROOT_MAX;
    set_live(&request.live);

    assert(b2l_xnu8796_plan(&request, &output) == B2L_XNU8796_PLAN_OK);
    assert(output.virtual_base == 0xfffffff004000000ULL);
    assert(output.physical_base == 0x902000000ULL);
    assert(output.memory_size == 0x3c000000ULL);
    assert(output.published_end == 0x93e000000ULL);
    assert(output.physical_base - request.arena_base == 0x2000000ULL);
    assert(request.arena_base + request.arena_size - output.published_end ==
           0x2000000ULL);
    assert((output.physical_base & (B2L_XNU8796_INTERIOR_GUARD - 1)) == 0);
    assert((output.mach_header_pa & (B2L_XNU8796_PAGE_SIZE - 1)) == 0);
    assert((output.bootstrap_map_end & (B2L_XNU8796_L2_BLOCK - 1)) == 0);
    assert(output.kernel_base_pa == 0x905000000ULL);
    assert(output.kernel_base_pa - output.physical_base ==
           B2L_XNU8796_LINK_OFFSET);
    assert(output.mach_header_pa == 0x905004000ULL);
    assert(output.kernel_end_pa == 0x906230000ULL);
    assert(output.entry_pa == 0x90543b510ULL);
    assert(output.bootstrap_map_end == 0x93de00000ULL);
    assert(output.ramdisk_pa == 0x906230000ULL);
    assert(output.ramdisk_size == 0x4000000ULL);
    assert(output.device_tree_pa == 0x90a230000ULL);
    assert(output.device_tree_capacity == 0x10000ULL);
    assert(output.boot_args_pa == 0x90a240000ULL);
    assert(output.device_tree_pa >= output.ramdisk_pa + output.ramdisk_size);
    assert(output.boot_args_pa >=
           output.device_tree_pa + output.device_tree_capacity);
    assert((output.ramdisk_pa & (B2L_XNU8796_ROOT_ALIGN - 1)) == 0);
    assert((output.device_tree_pa & (B2L_XNU8796_ROOT_ALIGN - 1)) == 0);
    assert((output.boot_args_pa & (B2L_XNU8796_ROOT_ALIGN - 1)) == 0);
    assert(output.top_of_kernel_data == 0x90a244000ULL);
    assert(output.top_of_kernel_data <= output.bootstrap_map_end);

    /* The production loader passes the planner's published window and root
     * placement to the Apple DT writer. Exercise that handoff with the actual
     * Mach-O layout instead of an independently typed constant. */
    struct n986b_dt_config dt_config = {0};
    uint8_t dt[0x10000];
    size_t dt_used = 0;
    dt_config.memory_base = output.physical_base;
    dt_config.memory_size = output.memory_size;
    dt_config.ramdisk_base = output.ramdisk_pa;
    dt_config.ramdisk_size = output.ramdisk_size;
    dt_config.counter_frequency = 26000000;
    dt_config.video.base = 0xfa000000;
    dt_config.video.row_bytes = 5760;
    dt_config.video.height = 3088;
    assert(n986b_build_device_tree(dt, sizeof(dt), &dt_config, &dt_used) == 0);
    assert(dt_used > 0 && dt_used <= output.device_tree_capacity);
    uint64_t dt_va = output.virtual_base + output.device_tree_pa -
                     output.physical_base;
    assert(dt_va - output.virtual_base < output.memory_size);
    assert(output.device_tree_pa + dt_used <= output.boot_args_pa);

    struct b2l_macho parsed = {0};
    assert(b2l_parse_macho(kernel, (size_t)length, &parsed) == 0);
    for (unsigned i = 0; i < parsed.segment_count; ++i) {
        uint64_t physical = 0;
        assert(b2l_xnu8796_segment_physical(&output, &parsed.segments[i],
                                              &physical) == 0);
        assert(physical == output.physical_base +
                           parsed.segments[i].va - output.virtual_base);
        assert(physical >= output.mach_header_pa);
        assert(physical + parsed.segments[i].size <= output.kernel_end_pa);
    }
    struct b2l_segment invalid_segment = parsed.segments[0];
    invalid_segment.va = output.virtual_base - 0x1000;
    uint64_t rejected_pa = UINT64_MAX;
    assert(b2l_xnu8796_segment_physical(&output, &invalid_segment,
                                          &rejected_pa) != 0);
    assert(rejected_pa == UINT64_MAX);
    invalid_segment = parsed.segments[0];
    invalid_segment.va = output.virtual_base + B2L_XNU8796_LINK_OFFSET;
    assert(b2l_xnu8796_segment_physical(&output, &invalid_segment,
                                          &rejected_pa) != 0);
    invalid_segment = parsed.segments[0];
    invalid_segment.va = UINT64_MAX - 0xfff;
    assert(b2l_xnu8796_segment_physical(&output, &invalid_segment,
                                          &rejected_pa) != 0);
    invalid_segment = parsed.segments[0];
    invalid_segment.size = output.kernel_end_pa - output.mach_header_pa + 1;
    assert(b2l_xnu8796_segment_physical(&output, &invalid_segment,
                                          &rejected_pa) != 0);
    assert(output.top_of_kernel_data + B2L_XNU8796_FREE_HEADROOM <=
           output.published_end);

    request.ramdisk_size = 0;
    assert(b2l_xnu8796_plan(&request, &output) == B2L_XNU8796_PLAN_OK);
    assert(output.ramdisk_pa == 0 && output.ramdisk_size == 0);
    assert(output.device_tree_pa == 0x906230000ULL);
    request.ramdisk_size = B2L_XNU8796_ROOT_MAX;

    map[0].type = 7; /* ConventionalMemory is not an owned allocation. */
    expect_error(&request, B2L_XNU8796_PLAN_EFI_OWNER);
    map[0].type = 2;
    map[1].base += 4096; /* A one-page hole is rejected. */
    expect_error(&request, B2L_XNU8796_PLAN_EFI_OWNER);
    map[1].base -= 4096;
    map[1].base -= 4096; /* EFI alias intersects another descriptor. */
    expect_error(&request, B2L_XNU8796_PLAN_EFI_OWNER);
    map[1].base += 4096;

    request.live.efi_image.base = 0x900000000ULL;
    expect_error(&request, B2L_XNU8796_PLAN_LIVE_OVERLAP);
    set_live(&request.live);
    request.live.live_fdt.size = 0;
    expect_error(&request, B2L_XNU8796_PLAN_LIVE_OVERLAP);
    set_live(&request.live);
    request.live.upload_snapshot.base = UINT64_MAX - 1;
    expect_error(&request, B2L_XNU8796_PLAN_LIVE_OVERLAP);
    set_live(&request.live);

    request.ramdisk_size = B2L_XNU8796_ROOT_MAX + B2L_XNU8796_ROOT_ALIGN;
    expect_error(&request, B2L_XNU8796_PLAN_INPUT);
    request.ramdisk_size = B2L_XNU8796_ROOT_ALIGN + 1;
    expect_error(&request, B2L_XNU8796_PLAN_INPUT);
    request.ramdisk_size = B2L_XNU8796_ROOT_MAX;
    request.arena_base += 0x1000;
    expect_error(&request, B2L_XNU8796_PLAN_INPUT);
    request.arena_base -= 0x1000;
    request.kernel_file_size = 16;
    expect_error(&request, B2L_XNU8796_PLAN_MACHO);
    request.kernel_file_size = (size_t)length;

    /* The parser still accepts this page-aligned but different header VA;
     * the 8796-specific Scion layout contract must reject it. */
    uint64_t original_va;
    memcpy(&original_va, kernel + 32 + 24, sizeof(original_va));
    uint64_t shifted_va = original_va - 0x1000;
    memcpy(kernel + 32 + 24, &shifted_va, sizeof(shifted_va));
    expect_error(&request, B2L_XNU8796_PLAN_GEOMETRY);
    memcpy(kernel + 32 + 24, &original_va, sizeof(original_va));

    free(kernel);
    puts("XNU 8796 standalone layout planner: PASS (real Mach-O + negative cases)");
    return 0;
}
