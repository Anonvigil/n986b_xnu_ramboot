/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef N986B_XNU_DEVICE_TREE_H
#define N986B_XNU_DEVICE_TREE_H
#include "BootArgs.h"
#include "b2l_ro_store.h"

#define N986B_SEED_BYTES 256U
#define N986B_DT_SPACE   (64U * 1024U)

struct n986b_dt_config {
  b2l_u64 memory_base, memory_size, mpidr;
  /* Zero for the proven v1 boot; populated only by the isolated v2 loader. */
  b2l_u64 ramdisk_base, ramdisk_size;
  b2l_u32 counter_frequency;
  unsigned char random_seed[N986B_SEED_BYTES];
  struct b2l_boot_video video;
  /* Optional v3 manifest contract; NULL/zero preserves v1/v2 DT bytes. */
  const unsigned char *ro_store_v1;
  b2l_size ro_store_v1_size;
  struct b2l_ro_range ro_store_live[B2L_RO_LIVE_REGION_COUNT];
};

/* Apple flattened device tree, not an FDT. It describes only resources
 * currently established on N986B and does not claim Apple silicon identity. */
int n986b_build_device_tree(void *buffer, b2l_size capacity,
                            const struct n986b_dt_config *config,
                            b2l_size *used);
#endif
