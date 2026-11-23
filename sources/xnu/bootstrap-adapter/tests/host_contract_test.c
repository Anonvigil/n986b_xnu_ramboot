/* Host checks for the real N986B/XNU contracts, not a copy of the builder. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../Arena.h"
#include "../DeviceTree.h"
#include "../MachO.h"
#include "../Xnu8796Layout.h"

#define PUBLISHED_BASE (N986B_RAM_START + B2L_XNU8796_INTERIOR_GUARD)
#define PUBLISHED_SIZE (N986B_MIN_ARENA - 2 * B2L_XNU8796_INTERIOR_GUARD)

struct descriptor {
  uint32_t type, pad;
  uint64_t base, virtual_base, pages, attributes;
};
_Static_assert(sizeof(struct descriptor) == 40, "EFI descriptor wire size");

static uint8_t dt[65536];
static size_t used;
static unsigned root_match, cpu_match, owned_match, gic_match, gic_compatible_match, memory_match;
static unsigned cluster_match, cntfrq_match, cpu_ids_match, seed_match;
static unsigned bus_frequency_seen, cpu_frequency_seen;

static uint32_t get32(size_t at)
{
  uint32_t value;
  assert(at + 4 <= used);
  memcpy(&value, dt + at, 4);
  return value;
}
static uint64_t get64(const void *data)
{ uint64_t value; memcpy(&value, data, 8); return value; }
static void inspect(const char *path, const char *key, const uint8_t *value, uint32_t length)
{
  if (!strcmp(path, "") && !strcmp(key, "compatible")) {
    assert(!strcmp((const char *)value, "samsung,n986b")); ++root_match;
  }
  if (!strcmp(path, "/chosen") && !strcmp(key, "b2l,owned-ram-v1")) {
    assert(length == 16 && get64(value) == PUBLISHED_BASE &&
           get64(value + 8) == PUBLISHED_SIZE);
    ++owned_match;
  }
  if (!strcmp(path, "/chosen") && !strcmp(key, "random-seed")) {
    assert(length == N986B_SEED_BYTES && value[0] == 1 && value[255] == 0);
    ++seed_match;
  }
  if (!strcmp(path, "/memory") && !strcmp(key, "reg")) {
    assert(length == 16 && get64(value) == PUBLISHED_BASE &&
           get64(value + 8) == PUBLISHED_SIZE);
    ++memory_match;
  }
  if (!strcmp(path, "/cpus/cpu0") && !strcmp(key, "compatible")) {
    assert(!strcmp((const char *)value, "b2l,boot-cpu")); ++cpu_match;
  }
  if (!strcmp(path, "/cpus/cpu0") && !strcmp(key, "cluster-type")) {
    assert(length == 4 && value[0] == 'E'); ++cluster_match;
  }
  if (!strcmp(path, "/cpus/cpu0") &&
      (!strcmp(key, "reg") || !strcmp(key, "cpu-id") ||
       !strcmp(key, "cluster-id") || !strcmp(key, "die-cluster-id") ||
       !strcmp(key, "cluster-core-id"))) {
    uint32_t id; memcpy(&id, value, 4);
    assert(length == 4 && id == 0); ++cpu_ids_match;
  }
  if (!strcmp(path, "/cpus/cpu0") && !strcmp(key, "timebase-frequency")) {
    uint32_t frequency; memcpy(&frequency, value, 4);
    assert(frequency == 26000000U); ++cntfrq_match;
  }
  if (!strcmp(path, "/cpus/cpu0") && !strcmp(key, "bus-frequency"))
    ++bus_frequency_seen;
  if (!strcmp(path, "/cpus/cpu0") && !strcmp(key, "clock-frequency"))
    ++cpu_frequency_seen;
  if (!strcmp(path, "/arm-io/gic") && !strcmp(key, "reg")) {
    assert(length == 32 && get64(value) == 0x101000 &&
           get64(value + 8) == 0x1000 &&
           get64(value + 16) == 0x102000 &&
           get64(value + 24) == 0x1000);
    ++gic_match;
  }
  if (!strcmp(path, "/arm-io/gic") && !strcmp(key, "compatible")) {
    /* This is the copied diagnostic DT, not a validated XNU 8796 GIC node.
     * The Scion patch selects its GICv2 path only for "arm,gic-400". */
    static const char diagnostic[] = "arm,cortex-a15-gic";
    static const char scion_gicv2[] = "arm,gic-400";
    assert(length == sizeof(diagnostic));
    assert(memcmp(value, diagnostic, length) == 0);
    assert(length != sizeof(scion_gicv2) ||
           memcmp(value, scion_gicv2, sizeof(scion_gicv2)) != 0);
    ++gic_compatible_match;
  }
}

static size_t walk(size_t pos, const char *parent)
{
  uint32_t properties, children;
  char name[33] = {0}, path[128];
  assert(pos + 8 <= used);
  properties = get32(pos); children = get32(pos + 4); pos += 8;
  assert(properties < 64 && children < 32);
  size_t property_start = pos;
  for (uint32_t i = 0; i < properties; ++i) {
    uint32_t length;
    assert(pos + 36 <= used);
    length = get32(pos + 32);
    assert(pos + 36 + length <= used);
    if (!strncmp((const char *)(dt + pos), "name", 32)) {
      assert(length > 0 && length <= 32);
      memcpy(name, dt + pos + 36, length);
    }
    pos += 36 + ((length + 3U) & ~3U);
  }
  assert(name[0]);
  if (!strcmp(name, "device-tree")) {
    assert(!parent[0]); path[0] = 0;
  } else {
    assert(strlen(parent) + strlen(name) + 2 < sizeof(path));
    snprintf(path, sizeof(path), "%s/%s", parent, name);
  }
  size_t scan = property_start;
  for (uint32_t i = 0; i < properties; ++i) {
    uint32_t length = get32(scan + 32);
    char key[33] = {0};
    memcpy(key, dt + scan, 32);
    inspect(path, key, dt + scan + 36, length);
    scan += 36 + ((length + 3U) & ~3U);
  }
  for (uint32_t i = 0; i < children; ++i) pos = walk(pos, path);
  return pos;
}

static void put32(uint8_t *p, size_t offset, uint32_t value)
{ memcpy(p + offset, &value, 4); }
static void put64(uint8_t *p, size_t offset, uint64_t value)
{ memcpy(p + offset, &value, 8); }

static void check_macho(void)
{
  uint8_t image[4096] = {0};
  struct b2l_macho parsed = {0};
  const uint64_t va = 0xfffffff007000000ULL;
  put32(image, 0, 0xfeedfacf);
  put32(image, 4, 0x0100000c);
  put32(image, 12, 2);
  put32(image, 16, 2);
  put32(image, 20, 72 + 288);
  put32(image, 32, 0x19);
  put32(image, 36, 72);
  put64(image, 32 + 24, va);
  put64(image, 32 + 32, sizeof(image));
  put64(image, 32 + 40, 0);
  put64(image, 32 + 48, sizeof(image));
  put32(image, 32 + 60, 5); /* executable and readable */
  put32(image, 104, 5); /* LC_UNIXTHREAD */
  put32(image, 108, 288);
  put32(image, 112, 6); /* ARM_THREAD_STATE64 */
  put32(image, 116, 68);
  put64(image, 104 + 16 + 32 * 8, va + 0x100);
  assert(b2l_parse_macho(image, sizeof(image), &parsed) == 0);
  assert(parsed.entry == va + 0x100 && parsed.segment_count == 1);
  /* The XNU 8796 build's trailing __LINKEDIT uses a non-page vm size. */
  put64(image, 32 + 32, sizeof(image) - 1);
  put64(image, 32 + 48, sizeof(image) - 1);
  assert(b2l_parse_macho(image, sizeof(image), &parsed) == 0);
  assert(parsed.segments[0].size == sizeof(image));
  put64(image, 32 + 32, sizeof(image));
  put64(image, 32 + 48, sizeof(image));
  put32(image, 24, 4); /* MH_DYLDLINK */
  assert(b2l_parse_macho(image, sizeof(image), &parsed) != 0);
  put32(image, 24, 0);
  put32(image, 16, 3);
  put32(image, 20, 72 + 288 + 16);
  put32(image, 392, 0xe); /* LC_LOAD_DYLINKER */
  put32(image, 396, 16);
  assert(b2l_parse_macho(image, sizeof(image), &parsed) != 0);
  put32(image, 392, 0x20); /* LC_LAZY_LOAD_DYLIB */
  assert(b2l_parse_macho(image, sizeof(image), &parsed) != 0);
  put32(image, 20, 72 + 288 + 80);
  put32(image, 392, 0xb); /* LC_DYSYMTAB with unresolved symbols */
  put32(image, 396, 80);
  put32(image, 392 + 28, 1);
  assert(b2l_parse_macho(image, sizeof(image), &parsed) != 0);
}

int main(void)
{
  check_macho();
  struct descriptor map[2] = {
    {7, 0, 0x920000000ULL, 0, 0x20000000ULL / 4096, 0},
    {7, 0, 0x900000000ULL, 0, 0x20000000ULL / 4096, 0},
  };
  struct n986b_arena_chunk chunks[N986B_MAX_CHUNKS];
  unsigned count = 0;
  uint64_t base = 0, size = 0;
  assert(n986b_choose_arena(map, sizeof(map), sizeof(map[0]), &base, &size) == 0);
  assert(base == N986B_RAM_START && size == N986B_MIN_ARENA);
  assert(n986b_arena_chunks(map, sizeof(map), sizeof(map[0]), base, size,
                            chunks, &count) == 0);
  assert(count == 2 && chunks[0].base == N986B_RAM_START &&
         chunks[1].base == 0x920000000ULL &&
         chunks[0].size == 0x20000000ULL &&
         chunks[1].size == 0x20000000ULL);
  map[0].type = map[1].type = 2;
  assert(n986b_validate_arena(map, sizeof(map), sizeof(map[0]),
                              base, size, 2) == 0);
  map[0].base += 4096;
  assert(n986b_validate_arena(map, sizeof(map), sizeof(map[0]),
                              base, size, 2) != 0); /* hole */
  map[0].base -= 4096;
  map[0].base -= 4096;
  assert(n986b_validate_arena(map, sizeof(map), sizeof(map[0]),
                              base, size, 2) != 0); /* overlap */

  struct n986b_dt_config config = {0};
  config.memory_base = PUBLISHED_BASE;
  config.memory_size = PUBLISHED_SIZE;
  assert((B2L_XNU8796_VIRT_BASE & (B2L_XNU8796_L2_BLOCK - 1)) ==
         (config.memory_base & (B2L_XNU8796_L2_BLOCK - 1)));
  assert((config.memory_size % B2L_XNU8796_L2_BLOCK) == 0);
  assert(config.memory_base + config.memory_size <= N986B_RAM_END);
  config.counter_frequency = 26000000;
  config.video.base = 0xFA000000;
  config.video.width = 1440;
  config.video.height = 3088;
  config.video.row_bytes = 5760;
  config.video.depth = 32;
  for (unsigned i = 0; i < N986B_SEED_BYTES; ++i)
    config.random_seed[i] = (unsigned char)(i + 1);
  assert(n986b_build_device_tree(dt, sizeof(dt), &config, &used) == 0);
  assert(used < sizeof(dt) && walk(0, "") == used);
  assert(root_match == 1 && cpu_match == 1 && owned_match == 1 &&
         memory_match == 1 && gic_match == 1 && gic_compatible_match == 1 &&
         cluster_match == 1 &&
         cntfrq_match == 1 && cpu_ids_match == 5 && seed_match == 1 &&
         bus_frequency_seen == 0 && cpu_frequency_seen == 0);
  puts("N986B Mach-O + arena + diagnostic Apple DT structure: PASS");
  puts("XNU 8796 GIC compatibility: BLOCKED (diagnostic compatible and GICC span)");
  puts("XNU 8796 CPU0 clock contract: BLOCKED (bus-frequency and clock-frequency absent)");
  return 0;
}
