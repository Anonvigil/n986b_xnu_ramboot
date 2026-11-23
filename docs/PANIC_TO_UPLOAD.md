# Historical XNU panic-to-Upload experiment

An isolated iOS 16.5/XNU 8796 RAM test on `SM-N986B / N986BXXU0ATE3` explored returning a panic record to Samsung S-Boot Upload without a requested button action. This branch predates the H6 high-RAM block-vnode result. Its [source patch](../sources/xnu/historical/panic-to-upload-ate3.patch) is preserved as a **historical delta**, not as a patch tested against the published H6 tree. The patch SHA-256 is `5da665c68bf57c399f72c7b9f60f152e8e94f2a15040cdfadf073a31904dbd21`.

## What was observed

The linked Mach-O (SHA-256 `c5475a04fdf1bc6bac6bcfb46f6046c90589218a8c82a54ea3b2c9725f9d2ea4`) reached a PID 1 `ENOEXEC` panic and logged `N986B AUTO UPLOAD: ATE3 DINFORM ECE9, SWRESET 2`. The host then observed `SEC_UPLOAD 04E8:685D`; S-Boot's new summary reported `DINFORM=0x0000ECE9` and `escape from exception loop` (`E_LOOP`). Twelve Upload A/B region pairs matched, including the same-run manifest, handoff snapshot and XNU log. The test subsequently returned to Android with warranty bit 0 and Verified Boot green.

Together those observations support an XNU panic → PMU reset → S-Boot `E_LOOP` → Upload sequence for that one firmware/build. The automated record does not independently witness physical button input, and it does not prove unattended recovery on every panic or device revision.

## Mechanism and boundary

The ATE3 bootloader was observed checking PMU `DINFORM` at physical `0x1586080C` for `0xECE9` and entering Upload on a match. Linux's [Exynos990 device tree](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/exynos/exynos990.dtsi) describes PMU base `0x15860000` and software-reset offset `0x3A00`, value 2. The isolated XNU patch pre-mapped a 16 KiB PMU window at startup. At `panic_spin_forever`, it flushed bounded log data, wrote the marker, then requested software reset with barriers. A timer-bounded fallback returned to the original panic path if reset did not occur.

This patch depends on N986B-specific loader-log and startup symbols from its experimental tree. It is **not** a drop-in patch for upstream Apple XNU, Scion's base, or the published H6 source patch. It was tested only with the resident ATE3 S-Boot and must be reviewed and re-paired with any new FD, Mach-O, manifest and device profile before a new live trial. Raw Upload data and unredacted receipts remain outside this public repository.

The same run still selected a synthetic root whose `/sbin/launchd` vnode exposed zero bytes, causing `ENOEXEC`. The later diagnostic HFS root and H6 storage work resolved different gates; automatic Upload did not itself advance iOS user space.
