[Defines]
  PLATFORM_NAME                  = c2s
  PLATFORM_GUID                  = 6f1397a0-7a50-49c6-a7c8-25b53d8a52e4
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/c2s-ios165-ro-store-h1-branch-host
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch/exynos990-ios165-ro-store-h1-branch-host.fdf
  DEVICE_DXE_FV_COMPONENTS       = Platform/Samsung/exynos990/exynos990.fdf.inc
  BROKEN_CNTFRQ_EL0              = 1

!include Platform/Samsung/exynos990/exynos990.dsc

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DENABLE_SIMPLE_INIT -DBROKEN_CNTFRQ_EL0=$(BROKEN_CNTFRQ_EL0) -DN986B_DXE_PROBE=1

[PcdsFixedAtBuild.common]
  # N986B S-Boot advertises and draws into this reserved 20 MiB framebuffer.
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferAddress|0xFA000000
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferWidth|1440
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferHeight|3088

  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|496

  gRenegadePkgTokenSpaceGuid.PcdDeviceVendor|"Samsung"
  gRenegadePkgTokenSpaceGuid.PcdDeviceProduct|"Galaxy Note20 Ultra 5G"
  gRenegadePkgTokenSpaceGuid.PcdDeviceCodeName|"c2s"

[LibraryClasses.common]
  PlatformMemoryMapLib|Platform/Samsung/exynos990/Research/Ios165Reserve/Map/PlatformMemoryMapLibN986BReserve.inf

[Components.common]
  Platform/Samsung/exynos990/Application/N986BRamPreflight/N986BRamPreflight.inf {
    <LibraryClasses>
      IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
      OpensslLib|CryptoPkg/Library/OpensslLib/OpensslLibCrypto.inf
      RngLib|MdePkg/Library/BaseRngLibTimerLib/BaseRngLibTimerLib.inf
  }
  # RAM-only XNU loader. Included in this isolated candidate FD before Shell.
  Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch/Loader/N986BXnuLoader.inf {
    <LibraryClasses>
      FdtLib|EmbeddedPkg/Library/FdtLib/FdtLib.inf
      IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
      OpensslLib|CryptoPkg/Library/OpensslLib/OpensslLibCrypto.inf
      RngLib|MdePkg/Library/BaseRngLibTimerLib/BaseRngLibTimerLib.inf
  }
