/* SPDX-License-Identifier: BSD-2-Clause */
#include "Arena.h"

static b2l_u32 r32(const b2l_u8 *p)
{ return (b2l_u32)p[0] | (b2l_u32)p[1] << 8 | (b2l_u32)p[2] << 16 | (b2l_u32)p[3] << 24; }
static b2l_u64 r64(const b2l_u8 *p)
{ return r32(p) | (b2l_u64)r32(p + 4) << 32; }
static b2l_u64 align_up(b2l_u64 x)
{ return (x + N986B_ARENA_ALIGN - 1) & ~(N986B_ARENA_ALIGN - 1); }
static b2l_u64 align_down(b2l_u64 x)
{ return x & ~(N986B_ARENA_ALIGN - 1); }

static int descriptor(const b2l_u8 *p, b2l_u64 *base, b2l_u64 *end)
{
  b2l_u64 pages = r64(p + 24);
  *base = r64(p + 8);
  if ((*base & 4095) || !pages || pages > (~(b2l_u64)0) / 4096 ||
      *base > (~(b2l_u64)0) - pages * 4096) return -1;
  *end = *base + pages * 4096;
  return 0;
}

static int valid_map(const void *map, b2l_size bytes, b2l_size stride)
{
  if (!map || stride < 40 || !bytes || bytes % stride) return 0;
  const b2l_u8 *p = map;
  for (b2l_size off = 0; off < bytes; off += stride) {
    b2l_u64 base, end;
    if (descriptor(p + off, &base, &end)) return 0;
  }
  return 1;
}

/* Return the one descriptor covering cursor, rejecting any intersecting alias
 * before the next boundary. The caller can distinguish a hole from bad map. */
static int next_coverage(const b2l_u8 *p, b2l_size bytes, b2l_size stride,
                         b2l_u64 cursor, b2l_u64 limit, b2l_u32 type,
                         b2l_u64 *next)
{
  unsigned matches = 0;
  b2l_u64 found = cursor;
  for (b2l_size off = 0; off < bytes; off += stride) {
    b2l_u64 base, end;
    if (descriptor(p + off, &base, &end)) return -1;
    if (base <= cursor && cursor < end) {
      if (r32(p + off) != type || (r64(p + off + 32) & (1ULL << 63))) return -1;
      ++matches;
      found = end < limit ? end : limit;
    }
  }
  if (matches > 1) return -1;
  if (!matches) return 0;
  for (b2l_size off = 0; off < bytes; off += stride) {
    b2l_u64 base, end;
    if (descriptor(p + off, &base, &end)) return -1;
    if (base < found && end > cursor && !(base <= cursor && cursor < end))
      return -1;
  }
  *next = found;
  return 1;
}

int n986b_validate_arena(const void *map, b2l_size bytes, b2l_size stride,
                         b2l_u64 base, b2l_u64 size, b2l_u32 type)
{
  b2l_u64 cursor, end, next;
  if (!valid_map(map, bytes, stride) || !size ||
      (base & (N986B_ARENA_ALIGN - 1)) ||
      (size & (N986B_ARENA_ALIGN - 1)) ||
      size < N986B_MIN_ARENA || size > N986B_MAX_ARENA ||
      base < N986B_RAM_START || base >= N986B_RAM_END ||
      size > N986B_RAM_END - base) return -1;
  end = base + size;
  for (cursor = base; cursor < end; cursor = next) {
    if (next_coverage(map, bytes, stride, cursor, end, type, &next) != 1 ||
        next <= cursor) return -1;
  }
  return 0;
}

int n986b_choose_arena(const void *map, b2l_size bytes, b2l_size stride,
                       b2l_u64 *base, b2l_u64 *size)
{
  const b2l_u8 *p = map;
  b2l_u64 best_base = 0, best_size = 0;
  if (!base || !size || !valid_map(map, bytes, stride)) return -1;
  for (b2l_size off = 0; off < bytes; off += stride) {
    b2l_u64 begin, stop, cursor, next, span;
    if (r32(p + off) != 7 || (r64(p + off + 32) & (1ULL << 63))) continue;
    if (descriptor(p + off, &begin, &stop)) return -1;
    if (begin < N986B_RAM_START) begin = N986B_RAM_START;
    begin = align_up(begin);
    if (begin >= N986B_RAM_END || begin >= stop) continue;
    cursor = begin;
    while (cursor < N986B_RAM_END) {
      int coverage = next_coverage(p, bytes, stride, cursor,
                                   N986B_RAM_END, 7, &next);
      if (coverage < 0) return -1;
      if (!coverage || next <= cursor) break;
      cursor = next;
    }
    if (cursor <= begin || cursor - begin <= N986B_EFI_HEADROOM) continue;
    span = align_down(cursor - begin - N986B_EFI_HEADROOM);
    if (span > N986B_MAX_ARENA) span = N986B_MAX_ARENA;
    if (span >= N986B_MIN_ARENA && span > best_size) {
      best_base = begin;
      best_size = span;
    }
  }
  if (!best_size) return -2;
  if (n986b_validate_arena(map, bytes, stride, best_base, best_size, 7)) return -1;
  *base = best_base;
  *size = best_size;
  return 0;
}

int n986b_arena_chunks(const void *map, b2l_size bytes, b2l_size stride,
                       b2l_u64 base, b2l_u64 size,
                       struct n986b_arena_chunk chunks[N986B_MAX_CHUNKS],
                       unsigned *count)
{
  const b2l_u8 *p = map;
  b2l_u64 cursor, end, next;
  unsigned n = 0;
  if (!chunks || !count || n986b_validate_arena(map, bytes, stride, base, size, 7))
    return -1;
  end = base + size;
  for (cursor = base; cursor < end; cursor = next) {
    if (n == N986B_MAX_CHUNKS ||
        next_coverage(p, bytes, stride, cursor, end, 7, &next) != 1 ||
        next <= cursor) return -1;
    chunks[n].base = cursor;
    chunks[n].size = next - cursor;
    ++n;
  }
  *count = n;
  return 0;
}
