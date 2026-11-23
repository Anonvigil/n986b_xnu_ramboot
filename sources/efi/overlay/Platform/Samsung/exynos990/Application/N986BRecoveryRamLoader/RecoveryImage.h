/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef N986B_RECOVERY_IMAGE_H
#define N986B_RECOVERY_IMAGE_H

typedef unsigned char b2r_u8;
typedef unsigned int b2r_u32;
typedef unsigned long long b2r_u64;

struct b2r_part {
  b2r_u64 offset;
  b2r_u64 size;
};

struct b2r_boot_image {
  b2r_u32 page_size;
  b2r_u32 header_version;
  b2r_u32 header_size;
  b2r_u32 kernel_addr_hint;
  b2r_u32 ramdisk_addr_hint;
  b2r_u64 dtb_addr_hint;
  b2r_u64 image_end;
  struct b2r_part kernel;
  struct b2r_part ramdisk;
  struct b2r_part second;
  struct b2r_part recovery_dtbo;
  struct b2r_part dtb;
};

struct b2r_dt_table {
  b2r_u32 total_size;
  b2r_u32 header_size;
  b2r_u32 entry_size;
  b2r_u32 entry_count;
  b2r_u32 entries_offset;
  b2r_u32 page_size;
};

struct b2r_dt_entry {
  b2r_u32 offset;
  b2r_u32 size;
  b2r_u32 id;
  b2r_u32 revision;
  b2r_u32 custom[4];
};

/* All functions only inspect caller-owned memory and never follow a pointer
 * derived from an unvalidated header field. Zero means success. */
int b2r_parse_boot_v2(const b2r_u8 *image, b2r_u64 bytes,
                      struct b2r_boot_image *out);
int b2r_parse_dt_table(const b2r_u8 *table, b2r_u64 bytes,
                       struct b2r_dt_table *out);
int b2r_dt_entry(const b2r_u8 *table, b2r_u64 bytes,
                 const struct b2r_dt_table *layout, b2r_u32 index,
                 struct b2r_dt_entry *out);
int b2r_validate_arm64_image(const b2r_u8 *kernel, b2r_u64 bytes,
                             b2r_u64 *text_offset, b2r_u64 *image_size,
                             b2r_u64 *flags);

#endif
