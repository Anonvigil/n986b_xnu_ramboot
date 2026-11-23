/* SPDX-License-Identifier: BSD-2-Clause */
#include "b2l_ro_store.h"
#ifdef B2L_UEFI_BUILD
#include <Library/BaseMemoryLib.h>
#define EINVAL 22
#define EROFS 30
static void *b2l_memcpy(void *dst, const void *src, size_t n)
{ CopyMem(dst, src, n); return dst; }
static void *b2l_memset(void *dst, int value, size_t n)
{ SetMem(dst, n, (UINT8)value); return dst; }
static int b2l_memcmp(const void *a, const void *b, size_t n)
{ return (int)CompareMem(a, b, n); }
#define memcpy b2l_memcpy
#define memset b2l_memset
#define memcmp b2l_memcmp
#else
#include <errno.h>
#include <string.h>
#endif

static const uint8_t magic[8] = { 'B', '2', 'L', 'R', 'O', 'V', '1', 0 };

static uint16_t le16(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16); }
static uint64_t le64(const uint8_t *p)
{ return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }
static void put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{ put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v)
{ put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

int b2l_ro_store_geometry(const struct b2l_ro_store *s)
{
    uint8_t digest_or = 0;
    if (!s || s->reservation_tag == 0 ||
        (s->base & (B2L_RO_STORE_PAGE - 1U)) != 0 ||
        (s->length & (B2L_RO_STORE_PAGE - 1U)) != 0 ||
        s->base < B2L_RO_STORE_BASE || s->base >= B2L_RO_STORE_END ||
        s->length == 0 || s->length > B2L_RO_STORE_MAX_DIAGNOSTIC ||
        s->length > B2L_RO_STORE_END - s->base ||
        /* This v1 gate is pinned to the first witness/diagnostic interval. */
        s->base != B2L_RO_STORE_BASE ||
        (s->image_kind == B2L_RO_KIND_WITNESS &&
         s->length != B2L_RO_STORE_PAGE) ||
        (s->image_kind == B2L_RO_KIND_DIAGNOSTIC &&
         s->length != B2L_RO_STORE_MAX_DIAGNOSTIC) ||
        (s->image_kind != B2L_RO_KIND_WITNESS &&
         s->image_kind != B2L_RO_KIND_DIAGNOSTIC)) return EINVAL;
    /* [0x940...,0x9e0...) cannot intersect [0x900...,0x940...). */
    if (s->base < B2L_RO_ARENA_END &&
        s->length > B2L_RO_ARENA_BASE - s->base) return EINVAL;
    for (unsigned i = 0; i < sizeof(s->image_sha256); ++i)
        digest_or |= s->image_sha256[i];
    return digest_or ? 0 : EINVAL;
}

int b2l_ro_store_disjoint_live(const struct b2l_ro_store *s,
                               const struct b2l_ro_range *regions,
                               size_t count)
{
    if (b2l_ro_store_geometry(s) || !regions ||
        count != B2L_RO_LIVE_REGION_COUNT) return EINVAL;
    for (size_t i = 0; i < count; ++i) {
        uint64_t end;
        if (regions[i].base == 0 || regions[i].length == 0 ||
            regions[i].length > UINT64_MAX - regions[i].base) return EINVAL;
        end = regions[i].base + regions[i].length;
        if (s->base < end && regions[i].base < s->base + s->length)
            return EINVAL;
    }
    return 0;
}

int b2l_ro_store_encode(uint8_t out[B2L_RO_STORE_V1_BYTES],
                        const struct b2l_ro_store *s)
{
    if (!out || b2l_ro_store_geometry(s)) return EINVAL;
    memset(out, 0, B2L_RO_STORE_V1_BYTES);
    memcpy(out, magic, sizeof(magic));
    put16(out + 8, 1);
    put16(out + 10, B2L_RO_STORE_V1_BYTES);
    put32(out + 12, B2L_RO_FLAGS);
    put64(out + 16, s->base);
    put64(out + 24, s->length);
    put32(out + 32, B2L_RO_STORE_SECTOR);
    put32(out + 36, s->image_kind);
    memcpy(out + 40, s->image_sha256, 32);
    put64(out + 72, s->reservation_tag);
    return 0;
}

int b2l_ro_store_decode(const void *wire, size_t length,
                        struct b2l_ro_store *out)
{
    struct b2l_ro_store s;
    const uint8_t *p = wire;
    if (!p || !out || length != B2L_RO_STORE_V1_BYTES ||
        memcmp(p, magic, sizeof(magic)) != 0 ||
        le16(p + 8) != 1 || le16(p + 10) != B2L_RO_STORE_V1_BYTES ||
        le32(p + 12) != B2L_RO_FLAGS ||
        le32(p + 32) != B2L_RO_STORE_SECTOR) return EINVAL;
    s.base = le64(p + 16);
    s.length = le64(p + 24);
    s.image_kind = le32(p + 36);
    memcpy(s.image_sha256, p + 40, 32);
    s.reservation_tag = le64(p + 72);
    if (b2l_ro_store_geometry(&s)) return EINVAL;
    *out = s;
    return 0;
}

int b2l_ro_store_span(const struct b2l_ro_store *s, uint64_t offset,
                      size_t length, uint64_t *pa)
{
    if (!pa || b2l_ro_store_geometry(s) || length == 0 ||
        (offset & (B2L_RO_STORE_SECTOR - 1U)) ||
        (length & (B2L_RO_STORE_SECTOR - 1U)) ||
        offset > s->length || length > s->length - offset) return EINVAL;
    /* Geometry has proved base+length does not overflow. */
    *pa = s->base + offset;
    return 0;
}

int b2l_ro_store_open_flags(int fflags, int fwrite_mask)
{ return (fflags & fwrite_mask) ? EROFS : 0; }
int b2l_ro_store_block_operation(int is_read)
{ return is_read ? 0 : EROFS; }
int b2l_ro_store_char_write(void)
{ return EROFS; }
int b2l_ro_store_mutating_ioctl(void)
{ return EROFS; }
