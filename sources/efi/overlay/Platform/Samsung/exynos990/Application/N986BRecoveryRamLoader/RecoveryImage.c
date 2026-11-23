/* SPDX-License-Identifier: BSD-2-Clause */
#include "RecoveryImage.h"

static b2r_u32 le32(const b2r_u8 *p) {
  return (b2r_u32)p[0] | ((b2r_u32)p[1] << 8) |
         ((b2r_u32)p[2] << 16) | ((b2r_u32)p[3] << 24);
}

static b2r_u64 le64(const b2r_u8 *p) {
  return (b2r_u64)le32(p) | ((b2r_u64)le32(p + 4) << 32);
}

static b2r_u32 be32(const b2r_u8 *p) {
  return ((b2r_u32)p[0] << 24) | ((b2r_u32)p[1] << 16) |
         ((b2r_u32)p[2] << 8) | (b2r_u32)p[3];
}

static int add(b2r_u64 a, b2r_u64 b, b2r_u64 *sum) {
  *sum = a + b;
  return *sum < a ? -1 : 0;
}

static int align_up(b2r_u64 value, b2r_u64 alignment, b2r_u64 *out) {
  b2r_u64 tmp;
  if (!alignment || (alignment & (alignment - 1)) ||
      add(value, alignment - 1, &tmp)) return -1;
  *out = tmp & ~(alignment - 1);
  return 0;
}

static int part(b2r_u64 at, b2r_u64 size, b2r_u64 total,
                b2r_u64 page, struct b2r_part *out, b2r_u64 *next) {
  b2r_u64 end;
  if (add(at, size, &end) || end > total ||
      align_up(end, page, next) || *next > total) return -1;
  out->offset = at;
  out->size = size;
  return 0;
}

int b2r_parse_boot_v2(const b2r_u8 *image, b2r_u64 bytes,
                      struct b2r_boot_image *out) {
  b2r_u32 page;
  b2r_u64 at;
  unsigned i;
  static const b2r_u8 magic[8] = {'A','N','D','R','O','I','D','!'};
  if (!image || !out || bytes < 1660) return -1;
  for (i = 0; i < 8; ++i) if (image[i] != magic[i]) return -2;
  page = le32(image + 36);
  if (page < 2048 || page > 65536 || (page & (page - 1)) ||
      le32(image + 40) != 2 || le32(image + 1644) != 1660 ||
      page > bytes || le32(image + 8) < 64 || !le32(image + 16) ||
      !le32(image + 1648)) return -3;
  out->page_size = page;
  out->header_version = 2;
  out->header_size = 1660;
  out->kernel_addr_hint = le32(image + 12);
  out->ramdisk_addr_hint = le32(image + 20);
  out->dtb_addr_hint = le64(image + 1652);
  at = page;
  if (part(at, le32(image + 8), bytes, page, &out->kernel, &at) ||
      part(at, le32(image + 16), bytes, page, &out->ramdisk, &at) ||
      part(at, le32(image + 24), bytes, page, &out->second, &at) ||
      part(at, le32(image + 1632), bytes, page,
           &out->recovery_dtbo, &at) ||
      part(at, le32(image + 1648), bytes, page, &out->dtb, &at)) return -4;
  out->image_end = at;
  if (le64(image + 1636) != out->recovery_dtbo.offset) return -5;
  return 0;
}

int b2r_parse_dt_table(const b2r_u8 *table, b2r_u64 bytes,
                       struct b2r_dt_table *out) {
  b2r_u64 entries_end;
  if (!table || !out || bytes < 32 || be32(table) != 0xd7b7ab1eU)
    return -1;
  out->total_size = be32(table + 4);
  out->header_size = be32(table + 8);
  out->entry_size = be32(table + 12);
  out->entry_count = be32(table + 16);
  out->entries_offset = be32(table + 20);
  out->page_size = be32(table + 24);
  if (out->total_size > bytes || out->header_size < 32 ||
      out->header_size > out->total_size || out->entry_size < 32 ||
      out->entry_count == 0 || out->entry_count > 64 ||
      out->entries_offset < out->header_size ||
      out->entries_offset > out->total_size ||
      !out->page_size || (out->page_size & (out->page_size - 1))) return -2;
  entries_end = (b2r_u64)out->entries_offset +
                (b2r_u64)out->entry_size * out->entry_count;
  if (entries_end > out->total_size) return -3;
  return 0;
}

int b2r_dt_entry(const b2r_u8 *table, b2r_u64 bytes,
                 const struct b2r_dt_table *layout, b2r_u32 index,
                 struct b2r_dt_entry *out) {
  b2r_u64 at, end;
  b2r_u32 i;
  if (!table || !layout || !out || index >= layout->entry_count ||
      bytes < layout->total_size) return -1;
  at = (b2r_u64)layout->entries_offset +
       (b2r_u64)index * layout->entry_size;
  if (at > layout->total_size || 32 > layout->total_size - at) return -2;
  out->size = be32(table + at);
  out->offset = be32(table + at + 4);
  out->id = be32(table + at + 8);
  out->revision = be32(table + at + 12);
  for (i = 0; i < 4; ++i) out->custom[i] = be32(table + at + 16 + i * 4);
  if (!out->size || out->offset < layout->header_size ||
      add(out->offset, out->size, &end) || end > layout->total_size)
    return -3;
  return 0;
}

int b2r_validate_arm64_image(const b2r_u8 *kernel, b2r_u64 bytes,
                             b2r_u64 *text_offset, b2r_u64 *image_size,
                             b2r_u64 *flags) {
  if (!kernel || !text_offset || !image_size || !flags || bytes < 64 ||
      kernel[0x38] != 'A' || kernel[0x39] != 'R' ||
      kernel[0x3a] != 'M' || kernel[0x3b] != 0x64) return -1;
  *text_offset = le64(kernel + 8);
  *image_size = le64(kernel + 16);
  *flags = le64(kernel + 24);
  if (*image_size < bytes || *image_size > 256ULL * 1024 * 1024 ||
      *text_offset > 16ULL * 1024 * 1024 ||
      (*text_offset & 0xfff)) return -2;
  return 0;
}
