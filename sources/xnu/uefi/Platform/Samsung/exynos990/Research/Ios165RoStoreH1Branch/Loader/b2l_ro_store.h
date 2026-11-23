/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_RO_STORE_H
#define B2L_RO_STORE_H

#ifdef B2L_UEFI_BUILD
#include <Uefi.h>
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef UINTN size_t;
#define UINT64_C(x) x##ULL
#define UINT64_MAX MAX_UINT64
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* Explicit little-endian wire ABI, independent of C struct packing. */
#define B2L_RO_STORE_V1_BYTES 80U
#define B2L_RO_STORE_PAGE 4096U
#define B2L_RO_STORE_SECTOR 512U
#define B2L_RO_STORE_MAX_DIAGNOSTIC (1024U * 1024U)
#define B2L_RO_STORE_BASE UINT64_C(0x940000000)
#define B2L_RO_STORE_END  UINT64_C(0x9e0000000)
#define B2L_RO_ARENA_BASE UINT64_C(0x900000000)
#define B2L_RO_ARENA_END  UINT64_C(0x940000000)
#define B2L_RO_FLAG_READ_ONLY 1U
#define B2L_RO_FLAG_NO_EXEC   2U
#define B2L_RO_FLAGS (B2L_RO_FLAG_READ_ONLY | B2L_RO_FLAG_NO_EXEC)
#define B2L_RO_KIND_WITNESS 1U
#define B2L_RO_KIND_DIAGNOSTIC 2U

struct b2l_ro_store {
    uint64_t base;
    uint64_t length;
    uint64_t reservation_tag;
    uint32_t image_kind;
    uint8_t image_sha256[32];
};

enum { B2L_RO_LIVE_REGION_COUNT = 6 };
struct b2l_ro_range { uint64_t base, length; };

/* Caller supplies EFI image/FD, stack, live FDT, USB stream, Upload snapshot,
 * and framebuffer. All six must be present and disjoint from the image. */
int b2l_ro_store_disjoint_live(const struct b2l_ro_store *store,
                               const struct b2l_ro_range *regions,
                               size_t region_count);

/* Return 0 on acceptance. All other values are rejection. The digest is a
 * claimed image identity; the caller must compare it with independently
 * computed bytes before exposing a device. */
int b2l_ro_store_encode(uint8_t out[B2L_RO_STORE_V1_BYTES],
                        const struct b2l_ro_store *store);
int b2l_ro_store_decode(const void *wire, size_t length,
                        struct b2l_ro_store *out);
int b2l_ro_store_geometry(const struct b2l_ro_store *store);

/* A block request is sector-aligned and wholly within the image. Zero-length
 * requests are rejected so no end-of-device ambiguity enters the driver. */
int b2l_ro_store_span(const struct b2l_ro_store *store, uint64_t offset,
                      size_t length, uint64_t *physical_address);

/* Generic transport gates used by host tests and by the kernel adapter. */
int b2l_ro_store_open_flags(int fflags, int fwrite_mask);
int b2l_ro_store_block_operation(int is_read);
int b2l_ro_store_char_write(void);
int b2l_ro_store_mutating_ioctl(void);

#endif
