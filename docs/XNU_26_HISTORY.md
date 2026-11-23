# Earlier XNU 12377 branch (historical)

Before the version-matched [iOS 16.5 / XNU 8796 work](XNU_PORTING.md), this project explored a separate iPhoneOS 26.5 / XNU 12377.121.6 RAM boot on SM-N986B. Keep its ABI, memory layout, root images and failure reports separate from the 16.5 branch. Its 1,152-byte `boot_args` differs from the 736-byte structure used by XNU 8796.

The earlier branch established that the N986B volatile transport could move a large XNU/UEFI/root package into RAM and recover it through Upload-mode A/B reads. A corrected UEFI FD progressed from an initial missing XNU boot application and EFI Shell fallback to XNU boot tasks. The recorded screen and RAM results include HFS root, `launchd`, `fsck`, mount, dyld and code-signing/AMFI dependency failures at different experimental stages. Those were incremental diagnostics, not one completed boot. A full-package experiment acknowledged 4,265,284 USB OUT operations and verified the package after Upload, but the phone did not reach a usable iOS interface.

The arm64e user-space/caches of this branch also required CPU features not demonstrated by N986B CPU0. The later work therefore chose the iOS 16.5 20F66 / XNU 8796 line and an arm64 user-space entry gate. The older 26.5 panic screens must not be cited as proof of 16.5 behavior, and the older boot arguments or root package must not be dropped into the 8796 loader.

The detailed original work included private Apple-derived images and raw device captures that are not part of this GitHub repository. This note preserves the technical decision and observed boundary. The reusable current source and exact 16.5 status are in [the XNU source bundle](../sources/xnu/README.md) and [evidence ledger](XNU_EVIDENCE.md).
