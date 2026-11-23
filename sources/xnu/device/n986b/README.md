# N986B iOS 16.5 host-only configuration

> Historical first-stage configuration note. The later RAM-only port entered
> XNU 8796 and passed the H6 read-only block-vnode gate on SM-N986B. This
> page documents the initial config and its disabled optional drivers; see
> [the current XNU source guide](../../README.md) for the live status.

This is a source and configuration starting point, not a bootable N986B image. It uses the Scion common tree at commit `025f97557ac439651b08f2a906c01addfad77ece`, its iOS 16.5 profile (`xnu-8796.121.2`, Darwin 22.5.0), and the Exynos 990 SoC compile target. The upstream Exynos 990 layer explicitly says it has not booted.

The portable overlay is laid out as `devices/n986b/`; copy that directory into a checkout of the pinned Scion common commit before running the commands below. The common tree ignores new device directories by default, so a downstream repository that commits this overlay must explicitly track it.

The first target is a single CPU, RAM-resident root image and visible console using the already demonstrated N986B RAM UEFI transport. The configuration passes no boot arguments. It disables UFS, block/storage, all phone and Virtio drivers, and device firmware loading. It does not access the phone or alter its partitions or bootloader.

Host verification from the common tree, including within WSL Debian:

```sh
make n986b_defconfig
make config
python3 tools/hakios-device.py check n986b
python3 tools/hakios-config.py args
```

Expected at this stage: `drivers built in: none`, `boot-arguments:` with no value, and `firmware wanted from the phone: none`. These checks verify configuration consistency only. They do not compile XNU or prove a loader handoff.

## Integration gaps

1. The iOS 16.5 profile uses XNU's `BCM2837` machine build configuration. That is a source build setting and does not establish N986B runtime machine support. The patched kernel must be built and its ARM64 Mach-O inspected before any RAM handoff.
2. Scion's EFI `xnuloader` expects an `XNUP` package starting at `0xF0000000` or a volume file, and selects a platform from a compiled `struct platform` using the GOP framebuffer base or device-tree compatible string. It has no N986B platform. Its Galaxy S9 platform uses a different framebuffer and memory map. The existing N986B UEFI app uses its own package, RAM ownership and handoff. An N986B adapter or platform port, with exact memory ownership, GIC, timer and framebuffer validation, is required.
3. The first run must use only a RAM root and console. UFS, GPU, USB and other hardware drivers require separate evidence and tests. iOS 16.5 user space is not included in this configuration.
4. The existing N986B XNU 12377 UEFI app declares a 1,152-byte `boot_args` (`command_line[1024]`), while Scion's XNU 8796 loader declares a 736-byte `boot_args` (`commandLine[608]`). The command-line field begins at offset 108 in both, but the flags and actual-memory fields move from 1136/1144 to 720/728. The old handoff must be replaced or made version-aware; swapping only the Mach-O is incorrect. Run the overlay's `verify_boot_args.py` with `--scion-loader` and `--existing-loader-header` paths to compile static assertions against both source declarations.

Do not transfer this tree to the device or treat a successful host config check as boot verification.
