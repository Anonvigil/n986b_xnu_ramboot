/* SPDX-License-Identifier: BSD-2-Clause */
#include "DeviceTree.h"
#include "Arena.h"
#include "Xnu8796Layout.h"

struct writer { unsigned char *buf; b2l_size size, pos; int error; };
struct node { b2l_size offset; b2l_u32 properties, children; };

static void bytes(struct writer *w, const void *data, b2l_size length)
{
  const unsigned char *p = data;
  if (w->error || w->pos > w->size || length > w->size - w->pos) {
    w->error = 1; return;
  }
  for (b2l_size i = 0; i < length; ++i) w->buf[w->pos++] = p ? p[i] : 0;
}
static void u32(struct writer *w, b2l_u32 value)
{ bytes(w, &value, sizeof(value)); }
static b2l_size string_size(const char *s)
{ b2l_size n = 0; while (s[n]) ++n; return n + 1; }
static void prop(struct writer *w, struct node *n, const char *name,
                 const void *data, b2l_u32 size)
{
  b2l_size len = string_size(name);
  if (len > 32) { w->error = 1; return; }
  bytes(w, name, len); bytes(w, 0, 32 - len);
  u32(w, size); bytes(w, data, size); bytes(w, 0, (4 - (size & 3)) & 3);
  ++n->properties;
}
static void text_prop(struct writer *w, struct node *n,
                      const char *key, const char *value)
{ prop(w, n, key, value, (b2l_u32)string_size(value)); }
static struct node begin(struct writer *w, struct node *parent, const char *name)
{
  struct node n = { w->pos, 0, 0 };
  if (parent) ++parent->children;
  u32(w, 0); u32(w, 0);
  text_prop(w, &n, "name", name);
  return n;
}
static void end(struct writer *w, const struct node *n)
{
  b2l_size saved = w->pos;
  if (w->error) return;
  w->pos = n->offset;
  u32(w, n->properties); u32(w, n->children);
  w->pos = saved;
}

int n986b_build_device_tree(void *buffer, b2l_size capacity,
                            const struct n986b_dt_config *c, b2l_size *used)
{
  struct writer w = { buffer, capacity, 0, 0 };
  struct node root, chosen, memory_map, manifest, defaults, memory,
              cpus, cpu, arm_io, gic, display, fb, aliases;
  b2l_u32 zero = 0, one = 1, two = 2;
  b2l_u32 cpu_reg, cpu_cluster_type = 'E';
  b2l_u64 memory_reg[2], io_ranges[3] = { 0, 0x10000000ULL, 0x20000000ULL };
  b2l_u64 ramdisk_reg[2];
  b2l_u64 gic_regs[4] = { 0x101000, 0x1000, 0x102000, 0x1000 };
  b2l_u64 fb_reg[2], display_bytes;

  if (!buffer || !c || !used || capacity < 8 ||
      c->memory_base != N986B_RAM_START + B2L_XNU8796_INTERIOR_GUARD ||
      c->memory_size != N986B_MIN_ARENA -
                        2 * B2L_XNU8796_INTERIOR_GUARD ||
      (c->ramdisk_size == 0 && c->ramdisk_base != 0) ||
      (c->ramdisk_size != 0 &&
       ((c->ramdisk_base & (B2L_XNU8796_ROOT_ALIGN - 1)) != 0 ||
        (c->ramdisk_size & (B2L_XNU8796_ROOT_ALIGN - 1)) != 0 ||
        c->ramdisk_base < c->memory_base ||
        c->ramdisk_base - c->memory_base > c->memory_size ||
        c->ramdisk_size > c->memory_size -
                           (c->ramdisk_base - c->memory_base))) ||
      !c->counter_frequency ||
      c->video.height == 0 ||
      c->video.row_bytes > (~(b2l_u64)0) / c->video.height) return -1;
  display_bytes = c->video.row_bytes * c->video.height;
  if (c->video.base != 0xFA000000ULL || display_bytes > 0x1400000ULL)
    return -1;
  memory_reg[0] = c->memory_base;
  memory_reg[1] = c->memory_size;
  ramdisk_reg[0] = c->ramdisk_base;
  ramdisk_reg[1] = c->ramdisk_size;
  fb_reg[0] = c->video.base;
  fb_reg[1] = display_bytes;
  cpu_reg = (b2l_u32)(c->mpidr & 0xffffULL);

  root = begin(&w, 0, "device-tree");
  text_prop(&w, &root, "model", "Samsung Galaxy Note20 Ultra 5G");
  text_prop(&w, &root, "compatible", "samsung,n986b");
  text_prop(&w, &root, "target-type", "C2S");
  prop(&w, &root, "#address-cells", &two, 4);
  prop(&w, &root, "#size-cells", &two, 4);

  chosen = begin(&w, &root, "chosen");
  prop(&w, &chosen, "random-seed", c->random_seed, N986B_SEED_BYTES);
  prop(&w, &chosen, "dram-base", &c->memory_base, 8);
  prop(&w, &chosen, "dram-size", &c->memory_size, 8);
  /* Publish the 960 MiB interior of the LoaderData-owned 1 GiB arena. */
  prop(&w, &chosen, "b2l,owned-ram-v1", memory_reg, sizeof(memory_reg));
  prop(&w, &chosen, "debug-enabled", &one, 4);
  /* Exynos990 has no Apple CTRR; XNU's pmap reads this explicit policy. */
  prop(&w, &chosen, "kernel-ctrr-to-be-enabled", &zero, 4);
  text_prop(&w, &chosen, "firmware-version", "N986B RAM EFI research loader");
  text_prop(&w, &chosen, "stdout-path", "/framebuffer");
  memory_map = begin(&w, &chosen, "memory-map");
  if (c->ramdisk_size)
    prop(&w, &memory_map, "RAMDisk", ramdisk_reg, sizeof(ramdisk_reg));
  end(&w, &memory_map);
  manifest = begin(&w, &chosen, "manifest-properties"); end(&w, &manifest);
  end(&w, &chosen);

  defaults = begin(&w, &root, "defaults");
  prop(&w, &defaults, "sleep-disabled", &one, 4);
  end(&w, &defaults);
  memory = begin(&w, &root, "memory");
  text_prop(&w, &memory, "device_type", "memory");
  prop(&w, &memory, "reg", memory_reg, sizeof(memory_reg));
  end(&w, &memory);
  cpus = begin(&w, &root, "cpus");
  prop(&w, &cpus, "#address-cells", &one, 4);
  prop(&w, &cpus, "#size-cells", &zero, 4);
  cpu = begin(&w, &cpus, "cpu0");
  text_prop(&w, &cpu, "device_type", "cpu");
  text_prop(&w, &cpu, "compatible", "b2l,boot-cpu");
  text_prop(&w, &cpu, "state", "running");
  prop(&w, &cpu, "reg", &cpu_reg, 4);
  prop(&w, &cpu, "cpu-id", &zero, 4);
  prop(&w, &cpu, "cluster-id", &zero, 4);
  prop(&w, &cpu, "die-cluster-id", &zero, 4);
  prop(&w, &cpu, "cluster-core-id", &zero, 4);
  prop(&w, &cpu, "cluster-type", &cpu_cluster_type, 4);
  prop(&w, &cpu, "timebase-frequency", &c->counter_frequency, 4);
  prop(&w, &cpu, "fixed-frequency", &c->counter_frequency, 4);
  end(&w, &cpu); end(&w, &cpus);

  arm_io = begin(&w, &root, "arm-io");
  text_prop(&w, &arm_io, "device_type", "soc");
  text_prop(&w, &arm_io, "compatible", "samsung,exynos990");
  prop(&w, &arm_io, "#address-cells", &two, 4);
  prop(&w, &arm_io, "#size-cells", &two, 4);
  prop(&w, &arm_io, "ranges", io_ranges, sizeof(io_ranges));
  gic = begin(&w, &arm_io, "gic");
  text_prop(&w, &gic, "compatible", "arm,cortex-a15-gic");
  text_prop(&w, &gic, "interrupt-controller", "master");
  prop(&w, &gic, "reg", gic_regs, sizeof(gic_regs));
  end(&w, &gic);
  display = begin(&w, &arm_io, "disp0");
  text_prop(&w, &display, "device_type", "display-subsystem");
  text_prop(&w, &display, "compatible", "samsung,c2s-display");
  prop(&w, &display, "reg", fb_reg, sizeof(fb_reg));
  end(&w, &display); end(&w, &arm_io);
  fb = begin(&w, &root, "framebuffer");
  text_prop(&w, &fb, "device_type", "display");
  text_prop(&w, &fb, "compatible", "simple-framebuffer");
  prop(&w, &fb, "reg", fb_reg, sizeof(fb_reg));
  prop(&w, &fb, "width", &c->video.width, 8);
  prop(&w, &fb, "height", &c->video.height, 8);
  prop(&w, &fb, "stride", &c->video.row_bytes, 8);
  text_prop(&w, &fb, "format", "x8r8g8b8");
  end(&w, &fb);
  aliases = begin(&w, &root, "aliases");
  text_prop(&w, &aliases, "display0", "/framebuffer");
  end(&w, &aliases);
  end(&w, &root);
  if (w.error) return -2;
  *used = w.pos;
  return 0;
}
