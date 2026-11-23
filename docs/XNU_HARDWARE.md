# N986B hardware inputs for the XNU 8796 port

This note separates values observed in the N986B's live device tree and Linux recovery logs from work that still needs an XNU driver. These inputs guide the iOS 16.5 port; they do not establish that the corresponding hardware works under XNU. The iOS 16.5 H6 live result is limited to CPU0, diagnostic root and read-only high-RAM block-vnode reads. See [the evidence record](XNU_EVIDENCE.md).

## Live board description

| Item | Observed input | Port implication |
| --- | --- | --- |
| Board and SoC | C2S / Exynos 990, `SM-N986B` | Keep N986B tables separate from the Exynos 9810 S9/S9+ ports. |
| Device tree | 0x8a000000, size 0x880e4 in the captured run | Exclude or copy the DTB before allocating overlapping UEFI memory. The address is a run observation, not a universal firmware constant. |
| Memory | About 7.873 GiB across 16 noncontiguous regions | Construct memory descriptors from actual intervals; do not describe it as one contiguous 8 GiB bank. |
| CPUs | Eight cores, MPIDRs 0x0–0x5 and 0x100–0x101 | Prove CPU0 timer and interrupt delivery before enabling secondary cores. |
| Interrupt controller | GIC distributor 0x10101000; CPU interface 0x10102000 | Reconcile firmware tables with the captured device tree and verify XNU interrupt routing. |
| Timer | 26 MHz; physical/secure/virtual/hyp GIC GSIVs 30/29/27/26 | Check the timer frequency and PPI translation against the running firmware. |
| Display | 1440×3088 at 32 bpp, stride 5760 bytes | A full framebuffer needs at least 0x10f6800 bytes; an 8 MiB descriptor is insufficient. This does not imply an XNU display driver. |
| GPU | Mali-G77 r0p1, ID 0x9080, MMIO 0x18500000 | The Exynos 9810 Mali-G72 driver cannot be enabled by an ID substitution. Power, MMU, job submission and graphics integration remain unproven in XNU. |
| Touch | `sec_ts` on HSI2C 0x108d0000, address 0x48, IRQ 468 | TWRP touch operation validates its Linux path only; XNU needs its own controller and power/IRQ integration. |
| UFS | Host controller 0x13100000 | H6's verified storage is a RAM-backed block device, not the UFS controller. Keep physical storage writes disabled until explicitly tested. |

The live DTB SHA-256 was `95ace983a1ebb35d12c9744eb76624151f01c6bcab609be4cdc4af3d9ff4e950`. That identifies the measured board-description input, not every N986B firmware revision.

## UEFI memory-map lessons

An earlier EDK II layout placed a heap at `0x80c50000..0x90000000`, overlapping the live DTB. A former pointer slot at `0x80000000` conflicted with a reserved debug page. The later memory map separates the DTB, FD, framebuffer and RAM payload; the loader validates its arena and store reservations before `ExitBootServices`. See [the H6 UEFI source](../sources/xnu/uefi/README.md). Do not reuse a single memory map from an old run without validating it against the current device-tree and Upload capture.

The older layout also exposed overlapping HLOS and display descriptors. Its framebuffer reservation was 8 MiB, less than the approximately 17 MiB required by the panel. Those are defects in that earlier layout, not a claim that correcting them alone produces working XNU graphics.

## Reusable Exynos work and its boundary

The pinned [Scion common kernel](https://github.com/Scion-Project/scion-kernel/tree/025f97557ac439651b08f2a906c01addfad77ece) contains an Exynos 990 compile target and iOS 16.5 profile. Its [SoC support matrix](https://github.com/Scion-Project/scion-kernel/blob/025f97557ac439651b08f2a906c01addfad77ece/docs/soc-support.md) calls out missing Exynos 990 hardware work. The [S9 device port](https://github.com/Scion-Project/scion-starlte/tree/d041c776f4565015aadcda28e5a3c2fc3c017bba) and independent [S9+ port](https://github.com/danstronics/ios-s9plus-exynos/tree/2d092398040f98685b0c5a07776df5e9f199b9dc) provide version-matched architecture and driver patterns, while their Exynos 9810 board tables are not N986B measurements. The N986B source bundle starts with optional hardware drivers disabled so CPU, root and storage gates can be measured separately.

The present record has no validated N986B XNU GPU, UFS, touch, panel, audio or secondary-CPU driver. Continue by adding one driver at a time with a same-run observable result and a return-to-Android check.
