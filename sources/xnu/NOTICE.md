# XNU source notices

- Apple's open-source XNU `xnu-8796.121.2` and HFS `hfs-627.100.6` retain
  their original notices and Apple Public Source License 2.0. A copy of that
  license is [included](LICENSE.APSL). The `0001` and `0002` patches modify
  their source; follow the notices in the files they affect.
- [Scion-Project/scion-kernel](https://github.com/Scion-Project/scion-kernel)
  supplies the pinned common port. Scion separates its XNU-derived material
  under APSL 2.0 from its original loader, device, tool and documentation
  code under MIT. Its third-party components have their own notices. Use its
  [`LICENSE`](https://github.com/Scion-Project/scion-kernel/blob/025f97557ac439651b08f2a906c01addfad77ece/LICENSE),
  [`LICENSE.APSL`](https://github.com/Scion-Project/scion-kernel/blob/025f97557ac439651b08f2a906c01addfad77ece/LICENSE.APSL),
  and [`THIRD_PARTY_LICENSES.md`](https://github.com/Scion-Project/scion-kernel/blob/025f97557ac439651b08f2a906c01addfad77ece/THIRD_PARTY_LICENSES.md).
- The copied bootstrap adapter and UEFI source have per-file copyright and
  SPDX notices, including `BSD-2-Clause-Patent` and `BSD-2-Clause`. Those
  tags take precedence over the root repository's MIT license.
- No Apple closed-source user-space files, IPSW, device firmware, RAM capture,
  or stock ECT bytes are in this source directory.
