/* Host-only inspection of a candidate XNU 8796 Mach-O via the real N986B parser. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Arena.h"
#include "../MachO.h"

#define SCION_BOOT_VIRT_BASE 0xFFFFFFF004000000ULL
#define SCION_LINK_OFFSET   0x03000000ULL
#define TARGET_4K_PAGE      0x1000ULL
#define TARGET_4K_L2_BLOCK  0x200000ULL
#define SCION_BASE_ALIGN    0x2000000ULL
#define PROPOSED_PHYS_BASE  (N986B_RAM_START + SCION_BASE_ALIGN)
#define PROPOSED_MEM_SIZE   (N986B_MIN_ARENA - 2 * SCION_BASE_ALIGN)
#define REQUIRED_HEADROOM   0x4000000ULL
#define MAX_ROOT_IMAGE      0x4000000ULL
#define MAX_DT_AND_ARGS     0x20000ULL
#define XNU_16K_ADVERTISE_THRESHOLD 0x40000000ULL

_Static_assert((SCION_BOOT_VIRT_BASE & (SCION_BASE_ALIGN - 1)) == 0,
               "Scion boot virtual base must meet its 32 MiB alignment");
_Static_assert((PROPOSED_PHYS_BASE & (SCION_BASE_ALIGN - 1)) == 0,
               "candidate physical base must meet Scion alignment");
_Static_assert(PROPOSED_PHYS_BASE + PROPOSED_MEM_SIZE <= N986B_RAM_END,
               "candidate must stay inside the UEFI-owned 1 GiB arena");
_Static_assert(PROPOSED_PHYS_BASE >= N986B_XNU_PHYS_BASE &&
               PROPOSED_PHYS_BASE + PROPOSED_MEM_SIZE <=
                   N986B_XNU_PHYS_BASE + N986B_XNU_PHYS_SIZE,
               "candidate must remain inside the older published 992 MiB window");
_Static_assert((PROPOSED_MEM_SIZE & (TARGET_4K_L2_BLOCK - 1)) == 0,
               "published candidate must consist of full 4 KiB L2 blocks");
_Static_assert(PROPOSED_MEM_SIZE <= XNU_16K_ADVERTISE_THRESHOLD,
               "this candidate follows arm_init.c's hardware-page-size branch");

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    unsigned char *image;
    struct b2l_macho macho = {0};
    int result;
    if (argc != 2 || !(file = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: %s kernel.macho\n", argv[0]);
        return 2;
    }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) || !(image = malloc((size_t)length))) {
        fclose(file);
        return 2;
    }
    if (fread(image, 1, (size_t)length, file) != (size_t)length) {
        free(image);
        fclose(file);
        return 2;
    }
    fclose(file);
    result = b2l_parse_macho(image, (size_t)length, &macho);
    printf("file_size=%ld parser_result=%d\n", length, result);
    if (result != 0) {
        free(image);
        return 1;
    }
    printf("parser_virtual_base=0x%016" PRIx64 "\n", macho.virtual_base);
    printf("virtual_end=0x%016" PRIx64 "\n", macho.virtual_end);
    printf("entry=0x%016" PRIx64 "\n", macho.entry);
    printf("span_from_parser_base=0x%" PRIx64 "\n", macho.virtual_end - macho.virtual_base);
    printf("segments=%" PRIu32 "\n", macho.segment_count);
    for (unsigned i = 0; i < macho.segment_count; ++i) {
        const struct b2l_segment *s = &macho.segments[i];
        printf("segment[%u] va=0x%016" PRIx64 " vmsize=0x%" PRIx64
               " fileoff=0x%" PRIx64 " filesize=0x%" PRIx64 " prot=%u\n",
               i, s->va, s->size, s->file_offset, s->file_size, s->protection);
    }
    /* Perform the same bounded zero-then-copy operation used by the copied
     * UEFI loader against the real artifact, including partial __LINKEDIT. */
    uint64_t mapped_span = macho.virtual_end - macho.virtual_base;
    unsigned char *mapped = calloc(1, (size_t)mapped_span);
    if (!mapped) { free(image); return 2; }
    for (unsigned i = 0; i < macho.segment_count; ++i) {
        const struct b2l_segment *s = &macho.segments[i];
        uint64_t offset = s->va - macho.virtual_base;
        if (s->va < macho.virtual_base || offset > mapped_span ||
            s->size > mapped_span - offset || s->file_offset > (uint64_t)length ||
            s->file_size > (uint64_t)length - s->file_offset ||
            s->file_size > s->size) {
            fprintf(stderr, "segment copy bounds failed at %u\n", i);
            free(mapped); free(image); return 1;
        }
        memset(mapped + offset, 0, (size_t)s->size);
        memcpy(mapped + offset, image + s->file_offset, (size_t)s->file_size);
        if (memcmp(mapped + offset, image + s->file_offset, (size_t)s->file_size)) {
            fprintf(stderr, "segment copy mismatch at %u\n", i);
            free(mapped); free(image); return 1;
        }
        for (uint64_t j = s->file_size; j < s->size; ++j)
            if (mapped[offset + j]) {
                fprintf(stderr, "segment zero-fill mismatch at %u\n", i);
                free(mapped); free(image); return 1;
            }
    }
    free(mapped);
    puts("segment_copy_check=PASS");
    /* This is a host-side candidate layout, not a firmware setting. The
     * compile-only EXYNOS990 board omits __ARM_16K_PG__, selecting 4 KiB. */
    uint64_t first = UINT64_MAX;
    for (unsigned i = 0; i < macho.segment_count; ++i)
        if (macho.segments[i].va < first) first = macho.segments[i].va;
    if (macho.virtual_base != SCION_BOOT_VIRT_BASE + SCION_LINK_OFFSET ||
        first != SCION_BOOT_VIRT_BASE + SCION_LINK_OFFSET + 0x4000 ||
        N986B_RAM_END - N986B_RAM_START != N986B_MIN_ARENA ||
        (SCION_BOOT_VIRT_BASE & (TARGET_4K_L2_BLOCK - 1)) ||
        (PROPOSED_PHYS_BASE & (TARGET_4K_L2_BLOCK - 1)) ||
        (first & (TARGET_4K_PAGE - 1)) ||
        macho.virtual_end < SCION_BOOT_VIRT_BASE ||
        macho.virtual_end - SCION_BOOT_VIRT_BASE > PROPOSED_MEM_SIZE - REQUIRED_HEADROOM) {
        fprintf(stderr, "XNU 8796 host layout assertions failed\n");
        free(image);
        return 1;
    }
    /* start.s subtracts the Mach-header offset from memSize, then truncates
     * to complete L2 blocks when creating its first bootstrap mappings. */
    uint64_t header_pa = PROPOSED_PHYS_BASE + first - SCION_BOOT_VIRT_BASE;
    uint64_t remaining = PROPOSED_MEM_SIZE - (header_pa - PROPOSED_PHYS_BASE);
    uint64_t bootstrap_end = (header_pa & ~(TARGET_4K_L2_BLOCK - 1)) +
                             (remaining / TARGET_4K_L2_BLOCK) * TARGET_4K_L2_BLOCK;
    uint64_t kernel_end_pa = PROPOSED_PHYS_BASE + macho.virtual_end - SCION_BOOT_VIRT_BASE;
    if (header_pa - PROPOSED_PHYS_BASE != SCION_LINK_OFFSET + 0x4000 ||
        bootstrap_end > PROPOSED_PHYS_BASE + PROPOSED_MEM_SIZE ||
        kernel_end_pa > bootstrap_end ||
        MAX_ROOT_IMAGE + MAX_DT_AND_ARGS > bootstrap_end - kernel_end_pa) {
        fprintf(stderr, "XNU 8796 bootstrap block coverage failed\n");
        free(image);
        return 1;
    }
    printf("proposed_boot_args_virt_base=0x%016" PRIx64 "\n", (uint64_t)SCION_BOOT_VIRT_BASE);
    printf("owned_arena=0x%016" PRIx64 "..0x%016" PRIx64 "\n",
           (uint64_t)N986B_RAM_START, (uint64_t)N986B_RAM_END);
    printf("proposed_boot_args_phys_base=0x%016" PRIx64 "\n", (uint64_t)PROPOSED_PHYS_BASE);
    printf("proposed_boot_args_mem_size=0x%" PRIx64 "\n", (uint64_t)PROPOSED_MEM_SIZE);
    printf("proposed_published_end=0x%016" PRIx64 "\n",
           (uint64_t)(PROPOSED_PHYS_BASE + PROPOSED_MEM_SIZE));
    printf("proposed_page_size=0x%" PRIx64 " l2_block=0x%" PRIx64 "\n",
           (uint64_t)TARGET_4K_PAGE, (uint64_t)TARGET_4K_L2_BLOCK);
    puts("proposed_xnu_advertised_page_size=0x1000 (memSize <= 1GiB branch)");
    printf("proposed_bootstrap_map_end=0x%016" PRIx64 "\n", bootstrap_end);
    printf("proposed_kernel_pa_start=0x%016" PRIx64 "\n",
           (uint64_t)(PROPOSED_PHYS_BASE + first - SCION_BOOT_VIRT_BASE));
    printf("proposed_kernel_pa_end=0x%016" PRIx64 "\n",
           kernel_end_pa);
    printf("proposed_entry_pa=0x%016" PRIx64 "\n",
           (uint64_t)(PROPOSED_PHYS_BASE + macho.entry - SCION_BOOT_VIRT_BASE));
    printf("maximum_top_with_64MiB_headroom=0x%016" PRIx64 "\n",
           (uint64_t)(PROPOSED_PHYS_BASE + PROPOSED_MEM_SIZE - REQUIRED_HEADROOM));
    free(image);
    return 0;
}
