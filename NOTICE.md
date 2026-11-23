# Source notices

The XNU source deltas in [`sources/xnu/`](sources/xnu/README.md) build on [Apple open-source XNU](https://github.com/apple-oss-distributions/xnu/tree/xnu-8796.121.2), [Apple open-source HFS](https://github.com/apple-oss-distributions/hfs/tree/hfs-627.100.6), and [Scion's iOS 16.5 kernel](https://github.com/Scion-Project/scion-kernel/tree/025f97557ac439651b08f2a906c01addfad77ece). XNU and HFS derivatives retain their original notices and the Apple Public Source License 2.0. Scion's original loader/device/tools and separately licensed files retain their own terms. See the [XNU bundle notices](sources/xnu/NOTICE.md) and [APSL text](sources/xnu/LICENSE.APSL).

The EFI source scaffold in [`sources/efi/`](sources/efi/README.md) derives from an Exynos EDK II board tree, TianoCore EDK II and SimpleInit. Preserve its upstream BSD-style and LGPL notices and the [board-tree](sources/efi/UPSTREAM_LICENSE) and [SimpleInit](sources/efi/SIMPLEINIT_LICENSE) license texts. The XNU-specific UEFI files retain per-file notices, including BSD-2-Clause and BSD-2-Clause-Patent where marked. The root MIT license does not relicense any third-party or derivative source.

No Apple closed-source user-space bytes, IPSW, firmware/ECT, raw Upload capture or device identity is distributed here.
