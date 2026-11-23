/** @file
  N986B TeamWin recovery RAM-stream diagnostic and gated Linux handoff.
  The default image has no handoff. The separate branch image copies only to
  EFI-owned RAM and never writes a partition, UEFI variable or RPMB.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <libfdt.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#if N986B_RECOVERY_ENABLE_HANDOFF
#include <Protocol/LoadedImage.h>
#endif
#include "RecoveryImage.h"
#include "N986BFreshFullEct.h"

#ifndef N986B_RECOVERY_ENABLE_HANDOFF
#define N986B_RECOVERY_ENABLE_HANDOFF 0
#endif
#ifndef N986B_RECOVERY_UI_MODE
#define N986B_RECOVERY_UI_MODE 0
#endif
#ifndef N986B_RECOVERY_HOLD
#define N986B_RECOVERY_HOLD 0
#endif
#ifndef N986B_RECOVERY_RELOCATED
#define N986B_RECOVERY_RELOCATED 0
#endif
#ifndef N986B_RECOVERY_UI_HIGH_STREAM
#define N986B_RECOVERY_UI_HIGH_STREAM 0
#endif
#ifndef N986B_EL2_INITCALL_BLACKLIST_DUAL
#define N986B_EL2_INITCALL_BLACKLIST_DUAL 0
#endif
#ifndef N986B_UI_HIGH_DUAL_BLACKLIST
#define N986B_UI_HIGH_DUAL_BLACKLIST 0
#endif
#ifndef N986B_RECOVERY_ECT_HOLD
#define N986B_RECOVERY_ECT_HOLD 0
#endif
#if N986B_RECOVERY_ECT_HOLD && !N986B_RECOVERY_ENABLE_HANDOFF
#error ECT hold must claim and verify the Linux destination pages
#endif
#if N986B_RECOVERY_ENABLE_HANDOFF != 0 && N986B_RECOVERY_ENABLE_HANDOFF != 1
#error N986B_RECOVERY_ENABLE_HANDOFF must be 0 or 1
#endif
#if N986B_RECOVERY_UI_MODE != 0 && N986B_RECOVERY_UI_MODE != 1
#error N986B_RECOVERY_UI_MODE must be 0 or 1
#endif
#if N986B_RECOVERY_UI_MODE && !N986B_RECOVERY_ENABLE_HANDOFF
#error UI mode requires Linux handoff
#endif
#if N986B_RECOVERY_HOLD && N986B_RECOVERY_ENABLE_HANDOFF
#error Hold diagnostic cannot include Linux handoff
#endif
#if N986B_RECOVERY_UI_HIGH_STREAM && (!N986B_RECOVERY_UI_MODE || !N986B_RECOVERY_ENABLE_HANDOFF)
#error High stream is only for the isolated Linux TWRP UI branch
#endif

#if N986B_RECOVERY_UI_HIGH_STREAM
#define B2R_MANIFEST_PA 0xA8000000ULL
#define B2R_IMAGE_PA 0xA8001000ULL
#define B2R_HOLD_STREAM_MARKER_PA 0xAC858000ULL
#define B2R_STREAM_END_EXPECTED 0xAC860000ULL
#define B2R_STREAM_LIMIT 0xAD000000ULL
#elif N986B_RECOVERY_RELOCATED
#define B2R_MANIFEST_PA 0x92200000ULL
#define B2R_IMAGE_PA 0x92201000ULL
#define B2R_HOLD_STREAM_MARKER_PA 0x95A88000ULL
#define B2R_STREAM_END_EXPECTED 0x95A90000ULL
#define B2R_STREAM_LIMIT 0x95D00000ULL
#else
#define B2R_MANIFEST_PA 0x90700000ULL
#define B2R_IMAGE_PA 0x90701000ULL
#define B2R_HOLD_STREAM_MARKER_PA 0x93F88000ULL
#define B2R_STREAM_END_EXPECTED 0x93F90000ULL
#define B2R_STREAM_LIMIT 0x95D00000ULL
#endif
#define B2R_MARKER_PA 0x95E00A00ULL
#define B2R_HOLD_MARKER_PA 0x95D00000ULL
#define B2R_SEC_DEBUG_BASE 0x91200000ULL
#define B2R_SEC_DEBUG_SIZE 0x1000000ULL
#define B2R_LIVE_FDT_PA 0x8A000000ULL
#define B2R_LIVE_FDT_LIMIT 0x100000U
#define B2R_IMAGE_BYTES 57640960ULL
#define B2R_MERGED_BYTES 528004ULL
#if N986B_RECOVERY_UI_MODE
#define B2R_DIAG_BYTES 17667760ULL
#else
#define B2R_DIAG_BYTES 1082531ULL
#endif
/* Exact Linux load targets in one 1 GiB RAM window. The no-branch build
 * checks them without allocation; the branch build claims and fills them. */
#define B2R_KERNEL_BASE 0xA0000000ULL
#define B2R_KERNEL_DEST 0xA0080000ULL
#define B2R_KERNEL_SPAN 0x3000000ULL
#define B2R_RAMDISK_DEST 0xA4000000ULL
#if N986B_RECOVERY_UI_MODE
#define B2R_RAMDISK_SPAN 0x2000000ULL
#else
#define B2R_RAMDISK_SPAN 0x200000ULL
#endif
#define B2R_FDT_DEST 0xA6000000ULL
#define B2R_FDT_SPAN 0x200000ULL
#define B2R_ECT_DEST 0xA7000000ULL
#define B2R_ECT_BYTES 0x4B000ULL
#define B2R_ECT_SPAN 0x50000ULL
STATIC CONST UINT8 mEctSha256[32] = {
  0x32,0xa6,0x42,0x07,0x4a,0xa5,0x16,0x8a,
  0xed,0xe2,0xce,0xde,0xdf,0x3a,0xf6,0x2c,
  0x13,0xcd,0x20,0xeb,0x92,0x12,0x51,0x7f,
  0xe5,0x17,0xbf,0xbf,0xbb,0xc7,0x0c,0xaa
};

#pragma pack(push, 1)
typedef struct {
  UINT8 Magic[8];
  UINT32 Version;
  UINT32 HeaderSize;
  UINT64 ImagePa;
  UINT64 ImageSize;
  UINT8 ImageSha256[32];
  UINT64 MergedFdtPa;
  UINT64 MergedFdtSize;
  UINT8 MergedFdtSha256[32];
  UINT64 StreamEnd;
  UINT32 OverlayIndex;
  UINT32 Flags;
  UINT64 DiagRamdiskPa;
  UINT64 DiagRamdiskSize;
  UINT8 DiagRamdiskSha256[32];
  UINT8 Reserved[4096 - 176];
} B2R_MANIFEST;
#pragma pack(pop)

typedef char B2R_MANIFEST_SIZE_CHECK[(sizeof(B2R_MANIFEST) == 4096) ? 1 : -1];

#if N986B_RECOVERY_ENABLE_HANDOFF
typedef struct { UINT64 Start, End; } B2R_CLEAN_RANGE;
VOID EFIAPI B2RLinuxHandoff(UINT64 Entry, UINT64 Fdt,
                            CONST B2R_CLEAN_RANGE *Ranges);
#endif

STATIC CONST UINT8 mManifestMagic[8] = {'N','9','8','6','B','R','C','O'};
STATIC CONST UINT8 mOfficialImageSha[32] = {
  0x8a,0x4d,0xc7,0x91,0x4d,0x5c,0xe0,0xb6,
  0xe2,0xda,0xe9,0x00,0x43,0x3a,0x85,0x3a,
  0xd3,0x5a,0x16,0x06,0xb5,0xa9,0x4f,0x3e,
  0x90,0x42,0xae,0x85,0xc6,0xf2,0xa7,0x71
};
STATIC CONST UINT8 mMergedFdtSha[32] = {
#if N986B_RECOVERY_UI_MODE
  0xf0,0x55,0xc5,0x91,0x58,0x15,0xa7,0x05,
  0xf1,0x96,0xfd,0x23,0x6f,0xf2,0x48,0x56,
  0x85,0x90,0x92,0xfc,0x88,0xfc,0xb4,0xa2,
  0xbe,0x3f,0xd0,0xe2,0xde,0x14,0x87,0x69
#else
  0x78,0xea,0xe5,0x00,0x0a,0x54,0x8e,0xdc,
  0xa8,0xe5,0xad,0x80,0xf7,0x3a,0x1b,0x28,
  0xb2,0xe4,0x43,0xb9,0xd2,0x6d,0xdd,0x10,
  0xf7,0xb0,0xe8,0x4a,0xda,0xc9,0xf3,0x5c
#endif
};
STATIC CONST UINT8 mDiagRamdiskSha[32] = {
#if N986B_RECOVERY_UI_MODE
  0x07,0x88,0x14,0xb1,0xc8,0xbf,0x94,0x15,
  0x3a,0x1c,0xdd,0xa1,0xa0,0x76,0x16,0x1e,
  0xb9,0xf5,0xab,0x68,0xcf,0x58,0xac,0xd7,
  0x69,0x10,0x76,0xde,0xe8,0x11,0xf4,0x36
#else
  0x10,0x4b,0xdb,0xdc,0x46,0xe5,0x5b,0xc8,
  0xed,0x37,0xba,0x7f,0xcb,0x3d,0x37,0x71,
  0x04,0x38,0x8d,0xa4,0x60,0x78,0x8e,0x8a,
  0x41,0x82,0x6d,0x94,0x51,0x98,0x65,0xe8
#endif
};

STATIC VOID Mark(UINTN Slot, UINT64 Value) {
  volatile UINT64 *Location = (volatile UINT64 *)(UINTN)(B2R_MARKER_PA + Slot);
  *Location = Value;
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Location, sizeof(*Location));
#if N986B_RECOVERY_HOLD
  Location = (volatile UINT64 *)(UINTN)(B2R_HOLD_MARKER_PA + Slot);
  *Location = Value;
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Location, sizeof(*Location));
  Location = (volatile UINT64 *)(UINTN)(B2R_HOLD_STREAM_MARKER_PA + Slot);
  *Location = Value;
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)Location, sizeof(*Location));
#endif
  MemoryFence();
}

STATIC BOOLEAN RangeInType(CONST VOID *Map, UINTN Bytes, UINTN Stride,
                           UINT64 Start, UINT64 Length,
                           EFI_MEMORY_TYPE Required) {
  UINTN Offset;
  if (!Map || Stride < sizeof(EFI_MEMORY_DESCRIPTOR) || !Length ||
      Start + Length < Start) return FALSE;
  for (Offset = 0; Offset + sizeof(EFI_MEMORY_DESCRIPTOR) <= Bytes;
       Offset += Stride) {
    CONST EFI_MEMORY_DESCRIPTOR *D =
      (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    UINT64 Size = (UINT64)D->NumberOfPages << EFI_PAGE_SHIFT;
    if (D->Type == Required && D->PhysicalStart <= Start &&
        Size >= Start - D->PhysicalStart &&
        Length <= Size - (Start - D->PhysicalStart)) return TRUE;
  }
  return FALSE;
}

STATIC EFI_STATUS CheckMemoryMap(UINT64 StreamEnd) {
  EFI_STATUS Status;
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN Bytes = 0, Key, Stride;
  UINT32 Version;
  Status = gBS->GetMemoryMap(&Bytes, NULL, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL) return EFI_DEVICE_ERROR;
  Bytes += 4096;
  Map = AllocatePool(Bytes);
  if (!Map) return EFI_OUT_OF_RESOURCES;
  Status = gBS->GetMemoryMap(&Bytes, Map, &Key, &Stride, &Version);
  if (!EFI_ERROR(Status)) {
#if N986B_RECOVERY_RELOCATED || N986B_RECOVERY_UI_HIGH_STREAM
    if (!RangeInType(Map, Bytes, Stride, B2R_SEC_DEBUG_BASE,
                     B2R_SEC_DEBUG_SIZE, EfiReservedMemoryType) ||
#else
    if (
#endif
        !RangeInType(Map, Bytes, Stride, B2R_MANIFEST_PA,
                     StreamEnd - B2R_MANIFEST_PA, EfiReservedMemoryType) ||
        !RangeInType(Map, Bytes, Stride, B2R_LIVE_FDT_PA,
                     B2R_LIVE_FDT_LIMIT, EfiReservedMemoryType) ||
        !RangeInType(Map, Bytes, Stride, 0x95E00000ULL,
                     0x1000, EfiReservedMemoryType) ||
#if N986B_RECOVERY_HOLD
        !RangeInType(Map, Bytes, Stride, B2R_HOLD_MARKER_PA,
                     0x1000, EfiReservedMemoryType) ||
        !RangeInType(Map, Bytes, Stride, B2R_HOLD_STREAM_MARKER_PA,
                     0x1000, EfiReservedMemoryType) ||
#endif
        !RangeInType(Map, Bytes, Stride, B2R_KERNEL_BASE,
                     B2R_KERNEL_SPAN,
                     EfiConventionalMemory) ||
        !RangeInType(Map, Bytes, Stride, B2R_RAMDISK_DEST,
                     B2R_RAMDISK_SPAN, EfiConventionalMemory) ||
        !RangeInType(Map, Bytes, Stride, B2R_FDT_DEST,
                      B2R_FDT_SPAN, EfiConventionalMemory) ||
        !RangeInType(Map, Bytes, Stride, B2R_ECT_DEST,
                      B2R_ECT_SPAN, EfiConventionalMemory))
      Status = EFI_COMPROMISED_DATA;
  }
  FreePool(Map);
  return Status;
}

#if N986B_RECOVERY_RELOCATED || N986B_RECOVERY_UI_HIGH_STREAM
/* ATE3 S-Boot's native sec_debug_next header.  This buffer is outside the
 * relocated stream and must survive the complete USB transfer unchanged.
 * Check a few fixed structural fields rather than volatile log contents. */
STATIC EFI_STATUS CheckSecDebugCanary(VOID) {
  CONST UINT64 *Header = (CONST UINT64 *)(UINTN)B2R_SEC_DEBUG_BASE;
  Mark(80, 0x986B5700ULL);
  if (B2R_MANIFEST_PA < B2R_SEC_DEBUG_BASE + B2R_SEC_DEBUG_SIZE)
    return EFI_COMPROMISED_DATA;
  InvalidateDataCacheRange((VOID *)(UINTN)B2R_SEC_DEBUG_BASE, 0x1000);
  if (Header[0] != 0x1212131311221133ULL ||
      Header[1] != 0x0001000200020002ULL) return EFI_COMPROMISED_DATA;
  Mark(80, 0x986B5701ULL);
  if (Header[2] != 0x9120AB00ULL || Header[3] != 0x200000ULL ||
      Header[4] != 0x9140AD00ULL || Header[5] != 0xF3CULL ||
      Header[6] != 0x9140BE00ULL || Header[7] != 0x10000ULL)
    return EFI_COMPROMISED_DATA;
  Mark(80, 0x986B5702ULL);
  Print(L"ATE3 sec_debug_next native header and first maps preserved\r\n");
  return EFI_SUCCESS;
}
#endif

STATIC BOOLEAN FdtPropertyEqual(CONST VOID *Left, CONST VOID *Right,
                                 CONST CHAR8 *Path, CONST CHAR8 *Name) {
  INT32 LeftNode = fdt_path_offset(Left, Path);
  INT32 RightNode = fdt_path_offset(Right, Path);
  INT32 LeftLength, RightLength;
  CONST VOID *LeftData, *RightData;
  if (LeftNode < 0 || RightNode < 0) return FALSE;
  LeftData = fdt_getprop(Left, LeftNode, Name, &LeftLength);
  RightData = fdt_getprop(Right, RightNode, Name, &RightLength);
  return LeftData && RightData && LeftLength > 0 &&
         LeftLength == RightLength &&
         !CompareMem(LeftData, RightData, (UINTN)LeftLength);
}

STATIC BOOLEAN Overlap(UINT64 Left, UINT64 LeftLength,
                       UINT64 Right, UINT64 RightLength) {
  return Left < Right + RightLength && Right < Left + LeftLength;
}

STATIC BOOLEAN FixedRegAvoidsTargets(CONST VOID *Reg, INT32 Length) {
  CONST fdt32_t *Cells = Reg;
  INT32 Index;
  if (!Reg || Length <= 0 || (Length % 12)) return FALSE;
  for (Index = 0; Index < Length / 4; Index += 3) {
    UINT64 Base = ((UINT64)fdt32_to_cpu(Cells[Index]) << 32) |
                    fdt32_to_cpu(Cells[Index + 1]);
    UINT64 Size = fdt32_to_cpu(Cells[Index + 2]);
    if (!Size || Base + Size < Base ||
        Overlap(Base, Size, B2R_KERNEL_BASE, B2R_KERNEL_SPAN) ||
        Overlap(Base, Size, B2R_RAMDISK_DEST, B2R_RAMDISK_SPAN) ||
        Overlap(Base, Size, B2R_FDT_DEST, B2R_FDT_SPAN) ||
        Overlap(Base, Size, B2R_ECT_DEST, B2R_ECT_SPAN)) return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN CheckLoaderMemreserve(CONST VOID *Fdt,
                                     BOOLEAN Runtime,
                                     UINT64 StreamEnd) {
  CONST UINT64 Bases[6] = {
    B2R_KERNEL_BASE, B2R_RAMDISK_DEST, B2R_FDT_DEST,
    0x95E00000ULL, B2R_MANIFEST_PA, B2R_ECT_DEST
  };
  CONST UINT64 Sizes[6] = {
    B2R_KERNEL_SPAN, B2R_RAMDISK_SPAN, B2R_FDT_SPAN,
    0x1000ULL, Runtime ? StreamEnd - B2R_MANIFEST_PA : 0,
    B2R_ECT_SPAN
  };
  INT32 Count = fdt_num_mem_rsv(Fdt), Index;
  BOOLEAN Found[6] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
  if (Count != (Runtime ? 6 : 3) ||
      (Runtime && (StreamEnd <= B2R_MANIFEST_PA ||
                   StreamEnd > B2R_STREAM_LIMIT))) return FALSE;
  for (Index = 0; Index < Count; ++Index) {
    UINT64 Base, Size;
    UINTN Target;
    if (fdt_get_mem_rsv(Fdt, Index, &Base, &Size)) return FALSE;
    for (Target = 0; Target < (Runtime ? 6U : 3U); ++Target)
      if (Base == Bases[Target] && Size == Sizes[Target]) {
        if (Found[Target]) return FALSE;
        Found[Target] = TRUE;
        break;
      }
    if (Target == (Runtime ? 6U : 3U)) return FALSE;
  }
  return Found[0] && Found[1] && Found[2] &&
         (!Runtime || (Found[3] && Found[4] && Found[5]));
}

STATIC BOOLEAN StorageNodeDisabled(CONST VOID *Fdt, CONST CHAR8 *Path) {
  INT32 Node = fdt_path_offset(Fdt, Path), Length;
  CONST CHAR8 *Status;
  if (Node < 0) return FALSE;
  Status = fdt_getprop(Fdt, Node, "status", &Length);
  return Status && Length == sizeof("disabled") &&
         !CompareMem(Status, "disabled", sizeof("disabled"));
}

STATIC BOOLEAN CheckLiveMemoryAndReservations(CONST VOID *Live,
                                               CONST VOID *Merged) {
  INT32 Root = fdt_path_offset(Live, "/");
  INT32 Reserved = fdt_path_offset(Live, "/reserved-memory");
  INT32 Node, Inner, Length;
  UINTN MemoryCount = 0, FixedCount = 0;
  CHAR8 Path[128];
  CONST VOID *Reg;
  STATIC CONST UINT8 LiveAbox[12] = {
    0,0,0,0, 0xf8,0,0,0, 0x02,0x80,0,0
  };
  STATIC CONST UINT8 MergedAbox[12] = {
    0,0,0,0, 0xf8,0,0,0, 0x02,0xa0,0,0
  };
  if (Root < 0 || Reserved < 0) return FALSE;
  fdt_for_each_subnode(Node, Live, Root) {
    CONST CHAR8 *Name = fdt_get_name(Live, Node, NULL);
    if (!Name || AsciiStrnCmp(Name, "memory@", 7)) continue;
    if (fdt_get_path(Live, Node, Path, sizeof(Path)) ||
        !FdtPropertyEqual(Live, Merged, Path, "reg")) return FALSE;
    ++MemoryCount;
  }
  fdt_for_each_subnode(Node, Live, Reserved) {
    if (fdt_get_path(Live, Node, Path, sizeof(Path))) return FALSE;
    Reg = fdt_getprop(Live, Node, "reg", &Length);
    if (Reg && Length > 0) {
      if (!FixedRegAvoidsTargets(Reg, Length)) return FALSE;
      if (!AsciiStrCmp(Path, "/reserved-memory/abox-rmem")) {
        INT32 Other = fdt_path_offset(Merged, Path), OtherLen;
        CONST VOID *OtherReg = Other < 0 ? NULL :
          fdt_getprop(Merged, Other, "reg", &OtherLen);
        if (Length != sizeof(LiveAbox) || !OtherReg ||
            OtherLen != sizeof(MergedAbox) ||
            CompareMem(Reg, LiveAbox, sizeof(LiveAbox)) ||
            CompareMem(OtherReg, MergedAbox, sizeof(MergedAbox))) return FALSE;
      } else if (!FdtPropertyEqual(Live, Merged, Path, "reg")) return FALSE;
      ++FixedCount;
    }
    fdt_for_each_subnode(Inner, Live, Node) {
      Reg = fdt_getprop(Live, Inner, "reg", &Length);
      if (!Reg || Length <= 0) continue;
      if (!FixedRegAvoidsTargets(Reg, Length)) return FALSE;
      if (fdt_get_path(Live, Inner, Path, sizeof(Path)) ||
          !FdtPropertyEqual(Live, Merged, Path, "reg")) return FALSE;
      ++FixedCount;
    }
  }
  return MemoryCount == 16 && FixedCount == 34;
}

STATIC EFI_STATUS CheckFdt(CONST VOID *Merged, UINTN MergedSize) {
  CONST VOID *Live = (CONST VOID *)(UINTN)B2R_LIVE_FDT_PA;
  CONST CHAR8 *LiveModel, *MergedModel;
  CONST VOID *LiveReg, *MergedReg, *LiveIrq, *MergedIrq;
  INT32 LiveRoot, MergedRoot, LiveMali, MergedMali;
  INT32 LiveLength, MergedLength, LiveRegLen, MergedRegLen;
  INT32 LiveIrqLen, MergedIrqLen;
  Mark(88, 0x986B55F0);
  if (fdt_check_header(Live) || fdt_check_header(Merged) ||
      fdt_totalsize(Live) > B2R_LIVE_FDT_LIMIT ||
      fdt_totalsize(Merged) != MergedSize) return EFI_COMPROMISED_DATA;
  Mark(88, 0x986B5501);
  LiveRoot = fdt_path_offset(Live, "/");
  MergedRoot = fdt_path_offset(Merged, "/");
  LiveMali = fdt_path_offset(Live, "/mali@18500000");
  MergedMali = fdt_path_offset(Merged, "/mali@18500000");
  if (LiveRoot < 0 || MergedRoot < 0 || LiveMali < 0 || MergedMali < 0)
    return EFI_NOT_FOUND;
  Mark(88, 0x986B5502);
  LiveModel = fdt_getprop(Live, LiveRoot, "model", &LiveLength);
  MergedModel = fdt_getprop(Merged, MergedRoot, "model", &MergedLength);
  if (!LiveModel || !MergedModel || LiveLength <= 0 ||
      LiveLength != MergedLength ||
      CompareMem(LiveModel, MergedModel, (UINTN)LiveLength))
    return EFI_UNSUPPORTED;
  Mark(88, 0x986B5503);
  LiveReg = fdt_getprop(Live, LiveMali, "reg", &LiveRegLen);
  MergedReg = fdt_getprop(Merged, MergedMali, "reg", &MergedRegLen);
  LiveIrq = fdt_getprop(Live, LiveMali, "interrupts", &LiveIrqLen);
  MergedIrq = fdt_getprop(Merged, MergedMali, "interrupts", &MergedIrqLen);
  if (!LiveReg || !MergedReg || !LiveIrq || !MergedIrq ||
      LiveRegLen <= 0 || LiveRegLen != MergedRegLen ||
      LiveIrqLen <= 0 || LiveIrqLen != MergedIrqLen ||
      CompareMem(LiveReg, MergedReg, (UINTN)LiveRegLen) ||
      CompareMem(LiveIrq, MergedIrq, (UINTN)LiveIrqLen))
    return EFI_UNSUPPORTED;
  Mark(88, 0x986B5504);
  Print(L"N986B model matched: %a\r\n", LiveModel);
  if (fdt_path_offset(Live, "/memory@80000000") < 0 ||
      fdt_path_offset(Live, "/reserved-memory/sboot") < 0 ||
      fdt_path_offset(Merged, "/memory@80000000") < 0 ||
      fdt_path_offset(Merged, "/reserved-memory/sboot") < 0)
    return EFI_COMPROMISED_DATA;
  Mark(88, 0x986B5505);
  if (!CheckLiveMemoryAndReservations(Live, Merged))
    return EFI_COMPROMISED_DATA;
  Mark(88, 0x986B5506);
  if (!CheckLoaderMemreserve(Merged, FALSE, 0))
    return EFI_COMPROMISED_DATA;
  Mark(88, 0x986B5507);
  if (!StorageNodeDisabled(Merged, "/ufs@0x13100000") ||
      !StorageNodeDisabled(Merged, "/ufs-srpmb") ||
      !StorageNodeDisabled(Merged, "/dwmmc2@132E0000"))
    return EFI_COMPROMISED_DATA;
  Mark(88, 0x986B5508);
  /* Host-side merged FDT contains live DRAM banks and fixed carve-outs.
   * Dynamic reserved-memory and Linux memblock allocation need a branch gate. */
  Mark(72, 0x986B53D0);
  Print(L"DTB has live memory/carveouts; dynamic memblock remains gated\r\n");
  return EFI_SUCCESS;
}

STATIC EFI_STATUS CheckStream(B2R_MANIFEST *Manifest,
                              struct b2r_boot_image *Boot) {
  UINT8 Digest[32];
  CONST UINT8 *Image, *Merged, *Diag;
  struct b2r_dt_table BaseTable, OverlayTable;
  struct b2r_dt_entry Entry;
  b2r_u64 TextOffset, KernelSize, KernelFlags;
  UINTN Index;
  InvalidateDataCacheRange((VOID *)(UINTN)B2R_MANIFEST_PA,
                           sizeof(*Manifest));
  CopyMem(Manifest, (CONST VOID *)(UINTN)B2R_MANIFEST_PA, sizeof(*Manifest));
  if (CompareMem(Manifest->Magic, mManifestMagic, 8) ||
      Manifest->Version != 2 || Manifest->HeaderSize != sizeof(*Manifest) ||
      Manifest->ImagePa != B2R_IMAGE_PA ||
      Manifest->ImageSize != B2R_IMAGE_BYTES ||
      Manifest->MergedFdtSize != B2R_MERGED_BYTES ||
      Manifest->DiagRamdiskSize != B2R_DIAG_BYTES ||
      Manifest->OverlayIndex != 4 ||
#if N986B_RECOVERY_ENABLE_HANDOFF
#if N986B_RECOVERY_UI_MODE
      Manifest->Flags != 2 ||
#else
      Manifest->Flags != 1 ||
#endif
#else
      Manifest->Flags != 0 ||
#endif
      CompareMem(Manifest->ImageSha256, mOfficialImageSha, 32) ||
      CompareMem(Manifest->MergedFdtSha256, mMergedFdtSha, 32) ||
      CompareMem(Manifest->DiagRamdiskSha256, mDiagRamdiskSha, 32) ||
      Manifest->StreamEnd > B2R_STREAM_LIMIT ||
      Manifest->StreamEnd <= B2R_IMAGE_PA ||
      (Manifest->StreamEnd & 0xffff)) return EFI_COMPROMISED_DATA;
#if N986B_RECOVERY_HOLD
  if (Manifest->StreamEnd != B2R_STREAM_END_EXPECTED ||
      Manifest->DiagRamdiskPa + Manifest->DiagRamdiskSize >
        B2R_HOLD_STREAM_MARKER_PA)
    return EFI_COMPROMISED_DATA;
#endif
  if (Manifest->MergedFdtPa < B2R_IMAGE_PA + B2R_IMAGE_BYTES ||
      (Manifest->MergedFdtPa & 0xfff) ||
      Manifest->MergedFdtPa > Manifest->StreamEnd ||
      Manifest->MergedFdtSize > Manifest->StreamEnd - Manifest->MergedFdtPa ||
      Manifest->DiagRamdiskPa < Manifest->MergedFdtPa + B2R_MERGED_BYTES ||
      (Manifest->DiagRamdiskPa & 0xfff) ||
      Manifest->DiagRamdiskPa > Manifest->StreamEnd ||
      Manifest->DiagRamdiskSize > Manifest->StreamEnd - Manifest->DiagRamdiskPa)
    return EFI_BAD_BUFFER_SIZE;
  for (Index = 0; Index < sizeof(Manifest->Reserved); ++Index)
    if (Manifest->Reserved[Index]) return EFI_COMPROMISED_DATA;
  Image = (CONST UINT8 *)(UINTN)Manifest->ImagePa;
  Merged = (CONST UINT8 *)(UINTN)Manifest->MergedFdtPa;
  Diag = (CONST UINT8 *)(UINTN)Manifest->DiagRamdiskPa;
  InvalidateDataCacheRange((VOID *)(UINTN)B2R_MANIFEST_PA,
                           (UINTN)(Manifest->StreamEnd - B2R_MANIFEST_PA));
  if (!Sha256HashAll(Image, (UINTN)Manifest->ImageSize, Digest) ||
      CompareMem(Digest, mOfficialImageSha, 32)) return EFI_CRC_ERROR;
  Mark(8, 0x986B5301);
  if (!Sha256HashAll(Merged, (UINTN)Manifest->MergedFdtSize, Digest) ||
      CompareMem(Digest, mMergedFdtSha, 32)) return EFI_CRC_ERROR;
  Mark(16, 0x986B5302);
  if (!Sha256HashAll(Diag, (UINTN)Manifest->DiagRamdiskSize, Digest) ||
      CompareMem(Digest, mDiagRamdiskSha, 32) ||
      Diag[0] != 0x1f || Diag[1] != 0x8b) return EFI_CRC_ERROR;
  Mark(80, 0x986B5307);
  if (b2r_parse_boot_v2(Image, Manifest->ImageSize, Boot) ||
      Boot->image_end != Manifest->ImageSize ||
      Boot->kernel.size != 37363724ULL ||
      Boot->ramdisk.size != 17747664ULL ||
      Boot->second.size || Boot->recovery_dtbo.size != 2181436ULL ||
      Boot->dtb.size != 340908ULL ||
      b2r_validate_arm64_image(Image + Boot->kernel.offset,
                               Boot->kernel.size, &TextOffset,
                               &KernelSize, &KernelFlags) ||
      TextOffset != 0x80000 || KernelSize != 0x2F02000 ||
      KernelFlags != 0xA ||
      Image[Boot->ramdisk.offset] != 0x1f ||
      Image[Boot->ramdisk.offset + 1] != 0x8b)
    return EFI_LOAD_ERROR;
  Mark(24, 0x986B5303);
  if (b2r_parse_dt_table(Image + Boot->dtb.offset, Boot->dtb.size,
                         &BaseTable) || BaseTable.entry_count != 1 ||
      b2r_dt_entry(Image + Boot->dtb.offset, Boot->dtb.size,
                   &BaseTable, 0, &Entry) ||
      Entry.size < sizeof(struct fdt_header) ||
      fdt_check_header(Image + Boot->dtb.offset + Entry.offset) ||
      (UINT32)fdt_totalsize(Image + Boot->dtb.offset + Entry.offset) !=
        Entry.size ||
      b2r_parse_dt_table(Image + Boot->recovery_dtbo.offset,
                         Boot->recovery_dtbo.size, &OverlayTable) ||
      OverlayTable.entry_count != 9 ||
      b2r_dt_entry(Image + Boot->recovery_dtbo.offset,
                   Boot->recovery_dtbo.size, &OverlayTable, 4, &Entry) ||
      Entry.custom[0] != 19 || Entry.custom[1] != 20 ||
      Entry.size < sizeof(struct fdt_header) ||
      fdt_check_header(Image + Boot->recovery_dtbo.offset + Entry.offset) ||
      (UINT32)fdt_totalsize(Image + Boot->recovery_dtbo.offset + Entry.offset) !=
        Entry.size) return EFI_LOAD_ERROR;
  Mark(32, 0x986B5304);
  return CheckFdt(Merged, (UINTN)Manifest->MergedFdtSize);
}

#if N986B_RECOVERY_ENABLE_HANDOFF
/* This N986B firmware leaves CNTFRQ_EL0 at zero, as independently measured
 * by the XNU loader.  Linux's DT arch-timer driver uses clock-frequency when
 * present.  Accept that workaround only for the same 26 MHz live and merged
 * timer nodes, and only when the virtual counter actually advances.  Do not
 * write CNTFRQ_EL0 from EL1; firmware may trap or ignore that write. */
STATIC BOOLEAN TimerNode26MHz(CONST VOID *Fdt) {
  INT32 Node = fdt_path_offset(Fdt, "/timer"), Length;
  CONST VOID *Property;
  UINT32 Raw;
  if (Node < 0 ||
      fdt_node_check_compatible(Fdt, Node, "arm,armv8-timer"))
    return FALSE;
  Property = fdt_getprop(Fdt, Node, "clock-frequency", &Length);
  if (!Property || Length != sizeof(Raw)) return FALSE;
  CopyMem(&Raw, Property, sizeof(Raw));
  return fdt32_to_cpu(Raw) == 26000000U;
}

STATIC EFI_STATUS ValidateArchTimer(CONST B2R_MANIFEST *Manifest) {
  UINT64 RawFrequency, CounterStart, CounterEnd, Delta;
  volatile UINTN Spin;
  Mark(104, 0x986B5600); /* Timer validation entered. */
  if (!TimerNode26MHz((CONST VOID *)(UINTN)B2R_LIVE_FDT_PA) ||
      !TimerNode26MHz((CONST VOID *)(UINTN)Manifest->MergedFdtPa))
    return EFI_UNSUPPORTED;
  Mark(104, 0x986B5601); /* Both timer nodes say 26 MHz. */
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(RawFrequency));
  Mark(112, RawFrequency);
  if (RawFrequency != 0 && RawFrequency != 26000000ULL)
    return EFI_UNSUPPORTED;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(CounterStart));
  for (Spin = 0; Spin < 100000; ++Spin)
    __asm__ volatile("yield" ::: "memory");
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(CounterEnd));
  Delta = CounterEnd - CounterStart;
  Mark(120, Delta);
  if (Delta == 0) return EFI_UNSUPPORTED;
  Mark(104, 0x986B5602); /* Bounded counter progress observed. */
  return EFI_SUCCESS;
}

STATIC UINT64 ReadSctlr(VOID) {
  UINT64 Value;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Value));
  return Value;
}

STATIC BOOLEAN IdentityPage(UINT64 Address, BOOLEAN Write) {
  UINT64 Par;
  if (!(ReadSctlr() & 1)) return FALSE;
  if (Write)
    __asm__ volatile("at s1e1w, %1\n\tisb\n\tmrs %0, par_el1"
                     : "=r"(Par) : "r"(Address) : "memory");
  else
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
                     : "=r"(Par) : "r"(Address) : "memory");
  return !(Par & 1) &&
         (Par & 0x0000FFFFFFFFF000ULL) ==
         (Address & 0x0000FFFFFFFFF000ULL);
}

STATIC BOOLEAN IdentityRange(UINT64 Address, UINT64 Size, BOOLEAN Write) {
  UINT64 Last, Page;
  if (!Size || Address > MAX_UINT64 - Size) return FALSE;
  Last = Address + Size - 1;
  for (Page = Address & ~4095ULL; Page <= Last; Page += 4096)
    if (!IdentityPage(Page, Write)) return FALSE;
  return IdentityPage(Last, Write);
}

STATIC EFI_STATUS AllocateExact(UINT64 Base, UINT64 Bytes) {
  EFI_PHYSICAL_ADDRESS Address = Base;
  EFI_STATUS Status;
  if ((Base | Bytes) & 0xfff) return EFI_INVALID_PARAMETER;
  Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
                              (UINTN)(Bytes >> EFI_PAGE_SHIFT), &Address);
  if (EFI_ERROR(Status)) return Status;
  if (Address != Base) {
    gBS->FreePages(Address, (UINTN)(Bytes >> EFI_PAGE_SHIFT));
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS CopyAndVerifyOriginalEct(VOID) {
  UINT8 Digest[32];
  VOID *Destination = (VOID *)(UINTN)B2R_ECT_DEST;
  if (B2R_ECT_DEST < 0x80000000ULL ||
      B2R_ECT_DEST + B2R_ECT_SPAN > 0xBAB00000ULL ||
      Overlap(B2R_ECT_DEST, B2R_ECT_SPAN,
              B2R_KERNEL_BASE, B2R_KERNEL_SPAN) ||
      Overlap(B2R_ECT_DEST, B2R_ECT_SPAN,
              B2R_RAMDISK_DEST, B2R_RAMDISK_SPAN) ||
      Overlap(B2R_ECT_DEST, B2R_ECT_SPAN,
              B2R_FDT_DEST, B2R_FDT_SPAN) ||
      Overlap(B2R_ECT_DEST, B2R_ECT_SPAN,
              B2R_MANIFEST_PA, B2R_STREAM_END_EXPECTED - B2R_MANIFEST_PA) ||
      !Sha256HashAll(mN986BFreshFullEct, (UINTN)B2R_ECT_BYTES, Digest) ||
      CompareMem(Digest, mEctSha256, sizeof(Digest)))
    return EFI_COMPROMISED_DATA;
  ZeroMem(Destination, (UINTN)B2R_ECT_SPAN);
  CopyMem(Destination, mN986BFreshFullEct, (UINTN)B2R_ECT_BYTES);
  WriteBackInvalidateDataCacheRange(Destination, (UINTN)B2R_ECT_SPAN);
  if (!Sha256HashAll(Destination, (UINTN)B2R_ECT_BYTES, Digest) ||
      CompareMem(Digest, mEctSha256, sizeof(Digest)))
    return EFI_COMPROMISED_DATA;
  Mark(128, 0x986B5801ULL);
  return EFI_SUCCESS;
}


/* The S-Boot framebuffer is still scanned out when Linux activates DPU
 * SysMMU. Register the stock physical carve-out with DSIM's existing
 * one-to-one IOVMM handover path, using the ISP's proven 32-bit DMA window. */
STATIC BOOLEAN FbHandoverPropIs(CONST VOID *Fdt, INT32 Node,
                               CONST CHAR8 *Name, CONST VOID *Expected,
                               INT32 ExpectedBytes) {
  INT32 Length;
  CONST VOID *Value = fdt_getprop(Fdt, Node, Name, &Length);
  return Value != NULL && Length == ExpectedBytes &&
         CompareMem(Value, Expected, ExpectedBytes) == 0;
}

STATIC BOOLEAN FbHandoverPropAbsent(CONST VOID *Fdt, INT32 Node,
                                   CONST CHAR8 *Name) {
  INT32 Length;
  return fdt_getprop(Fdt, Node, Name, &Length) == NULL &&
         Length == -FDT_ERR_NOTFOUND;
}

STATIC EFI_STATUS ApplyFbHandoverFdt(VOID *Fdt) {
  INT32 Fb, Dsim, Dpu, Isp;
  STATIC CONST UINT8 FbReg[12] = {
    0, 0, 0, 0, 0xFA, 0, 0, 0, 1, 0x40, 0, 0
  };
  STATIC CONST UINT8 DsimIommus[12] = {
    0, 0, 0, 0xE7, 0, 0, 0, 0xE8, 0, 0, 0, 0xE9
  };
  STATIC CONST UINT8 DomainClients[12] = {
    0, 0, 1, 0x4F, 0, 0, 1, 0x50, 0, 0, 1, 0x51
  };
  STATIC CONST UINT8 Window[8] = {
    0x10, 0, 0, 0, 0xEF, 0xFF, 0, 0
  };
  STATIC CONST UINT8 One[4] = {0, 0, 0, 1};
  STATIC CONST UINT8 Two[4] = {0, 0, 0, 2};

  Fb = fdt_path_offset(Fdt, "/reserved-memory/fb");
  Dsim = fdt_path_offset(Fdt, "/dsim@0x190C0000");
  Dpu = fdt_path_offset(Fdt, "/iommu-domain_dpu");
  Isp = fdt_path_offset(Fdt, "/iommu-domain_isp");
  if (Fb < 0 || Dsim < 0 || Dpu < 0 || Isp < 0 ||
      fdt_node_offset_by_phandle(Fdt, 0x450) != -FDT_ERR_NOTFOUND ||
      !FbHandoverPropIs(Fdt, Fb, "compatible", "fb,rmem", sizeof("fb,rmem")) ||
      !FbHandoverPropIs(Fdt, Fb, "reg", FbReg, sizeof(FbReg)) ||
      !FbHandoverPropAbsent(Fdt, Fb, "phandle") ||
      !FbHandoverPropIs(Fdt, Dsim, "iommus", DsimIommus, sizeof(DsimIommus)) ||
      !FbHandoverPropAbsent(Fdt, Dsim, "memory-region") ||
      !FbHandoverPropIs(Fdt, Dpu, "domain-clients", DomainClients,
                        sizeof(DomainClients)) ||
      !FbHandoverPropIs(Fdt, Dpu, "#address-cells", Two, sizeof(Two)) ||
      !FbHandoverPropIs(Fdt, Dpu, "#size-cells", One, sizeof(One)) ||
      !FbHandoverPropAbsent(Fdt, Dpu, "#dma-address-cells") ||
      !FbHandoverPropAbsent(Fdt, Dpu, "#dma-size-cells") ||
      !FbHandoverPropAbsent(Fdt, Dpu, "dma-window") ||
      !FbHandoverPropIs(Fdt, Isp, "#dma-address-cells", One, sizeof(One)) ||
      !FbHandoverPropIs(Fdt, Isp, "#dma-size-cells", One, sizeof(One)) ||
      !FbHandoverPropIs(Fdt, Isp, "dma-window", Window, sizeof(Window)))
    return EFI_COMPROMISED_DATA;

  if (fdt_setprop_string(Fdt, Fb, "compatible", "exynos,fb_handover"))
    return EFI_BAD_BUFFER_SIZE;
  Fb = fdt_path_offset(Fdt, "/reserved-memory/fb");
  if (Fb < 0 || fdt_setprop_u32(Fdt, Fb, "phandle", 0x450))
    return EFI_BAD_BUFFER_SIZE;
  Dsim = fdt_path_offset(Fdt, "/dsim@0x190C0000");
  if (Dsim < 0 || fdt_setprop_u32(Fdt, Dsim, "memory-region", 0x450))
    return EFI_BAD_BUFFER_SIZE;
  Dpu = fdt_path_offset(Fdt, "/iommu-domain_dpu");
  if (Dpu < 0 || fdt_setprop_u32(Fdt, Dpu, "#dma-address-cells", 1) ||
      fdt_setprop_u32(Fdt, Dpu, "#dma-size-cells", 1))
    return EFI_BAD_BUFFER_SIZE;
  Dpu = fdt_path_offset(Fdt, "/iommu-domain_dpu");
  if (Dpu < 0 || fdt_setprop(Fdt, Dpu, "dma-window", Window, sizeof(Window)))
    return EFI_BAD_BUFFER_SIZE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS CheckFbHandoverFdt(CONST VOID *Fdt) {
  INT32 Fb = fdt_path_offset(Fdt, "/reserved-memory/fb");
  INT32 Dsim = fdt_path_offset(Fdt, "/dsim@0x190C0000");
  INT32 Dpu = fdt_path_offset(Fdt, "/iommu-domain_dpu");
  STATIC CONST UINT8 Handle[4] = {0, 0, 4, 0x50};
  STATIC CONST UINT8 One[4] = {0, 0, 0, 1};
  STATIC CONST UINT8 Window[8] = {0x10, 0, 0, 0, 0xEF, 0xFF, 0, 0};
  if (Fb < 0 || Dsim < 0 || Dpu < 0 ||
      !FbHandoverPropIs(Fdt, Fb, "compatible", "exynos,fb_handover",
                        sizeof("exynos,fb_handover")) ||
      !FbHandoverPropIs(Fdt, Fb, "phandle", Handle, sizeof(Handle)) ||
      !FbHandoverPropIs(Fdt, Dsim, "memory-region", Handle, sizeof(Handle)) ||
      !FbHandoverPropIs(Fdt, Dpu, "#dma-address-cells", One, sizeof(One)) ||
      !FbHandoverPropIs(Fdt, Dpu, "#dma-size-cells", One, sizeof(One)) ||
      !FbHandoverPropIs(Fdt, Dpu, "dma-window", Window, sizeof(Window)))
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS PrepareLinuxFdt(CONST B2R_MANIFEST *Manifest,
                                  UINTN *UsedBytes) {
  VOID *Destination = (VOID *)(UINTN)B2R_FDT_DEST;
  CONST VOID *Source = (CONST VOID *)(UINTN)Manifest->MergedFdtPa;
  INT32 Chosen, Ect, Fmp, Dpp6, Length;
  CONST VOID *Start, *End;
  CONST CHAR8 *FmpStatus, *Dpp6Status;
  CONST VOID *Dpp6Reg, *Dpp6Iommus;
  CONST fdt32_t *Dpp6Attr;
  STATIC CONST UINT8 Dpp6ExpectedReg[24] = {
    0, 0, 0, 0, 0x19, 0x09, 0x70, 0, 0, 0, 0x10, 0,
    0, 0, 0, 0, 0x19, 0x08, 0, 0, 0, 0, 0x10, 0
  };
  STATIC CONST UINT8 Dpp6ExpectedIommus[12] = {
    0, 0, 0, 0xe7, 0, 0, 0, 0xe8, 0, 0, 0, 0xe9
  };
  CONST fdt32_t *EctAddress, *EctSize;
  UINT64 StartValue, EndValue;
  STATIC CONST CHAR8 BootArgs[] =
#if N986B_RECOVERY_UI_MODE
#if N986B_RECOVERY_UI_HIGH_STREAM
    "rdinit=/init ro nokaslr sec_debug_next=0x1000000@0x91200000 "
#if N986B_UI_HIGH_DUAL_BLACKLIST
    "initcall_blacklist=exynos_deliver_el2_crash_info,exynos_s2mpufd_init,exynos_gpu_cooling_init,samsung_abox_pci_driver_init,samsung_abox_driver_init,exynos_perf_gmc_init,exynos_gpu_profiler_init,dhd_module_init,sensor_ois_mcu_init,sensor_paf_pdp_init "
#endif
    "androidboot.hardware=exynos990 "
#else
    "rdinit=/init ro nokaslr androidboot.hardware=exynos990 "
#endif
    "androidboot.selinux=permissive buildvariant=eng "
    "console=ttySAC0,115200n8 loglevel=8 panic=-1";
#else
#if N986B_RECOVERY_RELOCATED
    "rdinit=/init ro nokaslr sec_debug_next=0x1000000@0x91200000 "
#if N986B_EL2_INITCALL_BLACKLIST
#if N986B_EL2_INITCALL_BLACKLIST_TRIPLE
    "initcall_blacklist=exynos_deliver_el2_crash_info,exynos_s2mpufd_init,exynos_acpm_binary_update "
#elif N986B_EL2_INITCALL_BLACKLIST_DUAL
    "initcall_blacklist=exynos_deliver_el2_crash_info,exynos_s2mpufd_init,exynos_gpu_cooling_init,samsung_abox_pci_driver_init,samsung_abox_driver_init,exynos_perf_gmc_init,exynos_gpu_profiler_init,dhd_module_init,sensor_ois_mcu_init,sensor_paf_pdp_init "
#else
    "initcall_blacklist=exynos_deliver_el2_crash_info "
#endif
#endif
    "console=ttySAC0,115200n8 loglevel=8 panic=-1";
#else
    "rdinit=/init ro nokaslr console=ttySAC0,115200n8 loglevel=8 panic=-1";
#endif
#endif
  if (fdt_open_into(Source, Destination, (INT32)B2R_FDT_SPAN))
    return EFI_BAD_BUFFER_SIZE;
  /* Keep UFS and the RAM payload untouched; prevent FMP FIPS from
   * waiting for the intentionally absent sda device. */
  Fmp = fdt_path_offset(Destination, "/fmp");
  if (Fmp < 0 || fdt_node_check_compatible(Destination, Fmp,
                                           "samsung,exynos-fmp"))
    return EFI_COMPROMISED_DATA;
  FmpStatus = fdt_getprop(Destination, Fmp, "status", &Length);
  if (FmpStatus != NULL || Length != -FDT_ERR_NOTFOUND ||
      fdt_setprop_string(Destination, Fmp, "status", "disabled"))
    return EFI_COMPROMISED_DATA;
  /* DPP6 is the ODMA/writeback DPP linked to display SysMMU IRQ83.
   * Admit exactly this FDT node before changing its status. */
  Dpp6 = fdt_path_offset(Destination, "/dpp@0x19097000");
  if (Dpp6 < 0 || fdt_node_check_compatible(Destination, Dpp6,
                                            "samsung,exynos9-dpp"))
    return EFI_COMPROMISED_DATA;
  Dpp6Reg = fdt_getprop(Destination, Dpp6, "reg", &Length);
  if (!Dpp6Reg || Length != sizeof(Dpp6ExpectedReg) ||
      CompareMem(Dpp6Reg, Dpp6ExpectedReg, sizeof(Dpp6ExpectedReg)))
    return EFI_COMPROMISED_DATA;
  Dpp6Attr = fdt_getprop(Destination, Dpp6, "attr", &Length);
  if (!Dpp6Attr || Length != sizeof(fdt32_t) ||
      fdt32_to_cpu(*Dpp6Attr) != 0x000A0010U)
    return EFI_COMPROMISED_DATA;
  Dpp6Iommus = fdt_getprop(Destination, Dpp6, "iommus", &Length);
  if (!Dpp6Iommus || Length != sizeof(Dpp6ExpectedIommus) ||
      CompareMem(Dpp6Iommus, Dpp6ExpectedIommus,
                 sizeof(Dpp6ExpectedIommus)))
    return EFI_COMPROMISED_DATA;
  Dpp6Status = fdt_getprop(Destination, Dpp6, "status", &Length);
  if (Dpp6Status != NULL || Length != -FDT_ERR_NOTFOUND ||
      fdt_setprop_string(Destination, Dpp6, "status", "disabled"))
    return EFI_COMPROMISED_DATA;
  if (EFI_ERROR(ApplyFbHandoverFdt(Destination)))
    return EFI_COMPROMISED_DATA;
  Ect = fdt_path_offset(Destination, "/ect");
  if (Ect < 0) return EFI_COMPROMISED_DATA;
  EctAddress = fdt_getprop(Destination, Ect, "parameter_address", &Length);
  if (!EctAddress || Length != sizeof(fdt32_t) ||
      fdt32_to_cpu(*EctAddress) != 0x90000000U)
    return EFI_COMPROMISED_DATA;
  EctSize = fdt_getprop(Destination, Ect, "parameter_size", &Length);
  if (!EctSize || Length != sizeof(fdt32_t) ||
      fdt32_to_cpu(*EctSize) != (UINT32)B2R_ECT_BYTES)
    return EFI_COMPROMISED_DATA;
  Chosen = fdt_path_offset(Destination, "/chosen");
  if (Chosen < 0 ||
      fdt_setprop_string(Destination, Chosen, "bootargs", BootArgs) ||
      fdt_setprop_u64(Destination, Chosen, "linux,initrd-start",
                      B2R_RAMDISK_DEST) ||
      fdt_setprop_u64(Destination, Chosen, "linux,initrd-end",
                      B2R_RAMDISK_DEST + Manifest->DiagRamdiskSize) ||
      fdt_setprop_u32(Destination, Ect, "parameter_address",
                      (UINT32)B2R_ECT_DEST) ||
      fdt_add_mem_rsv(Destination, 0x95E00000ULL, 0x1000ULL) ||
      fdt_add_mem_rsv(Destination, B2R_MANIFEST_PA,
                      Manifest->StreamEnd - B2R_MANIFEST_PA) ||
      fdt_add_mem_rsv(Destination, B2R_ECT_DEST, B2R_ECT_SPAN) ||
      fdt_pack(Destination) || fdt_check_header(Destination))
    return EFI_COMPROMISED_DATA;
  Fmp = fdt_path_offset(Destination, "/fmp");
  if (Fmp < 0) return EFI_COMPROMISED_DATA;
  FmpStatus = fdt_getprop(Destination, Fmp, "status", &Length);
  if (!FmpStatus || Length != sizeof("disabled") ||
      CompareMem(FmpStatus, "disabled", sizeof("disabled")))
    return EFI_COMPROMISED_DATA;
  Dpp6 = fdt_path_offset(Destination, "/dpp@0x19097000");
  if (Dpp6 < 0 || fdt_node_check_compatible(Destination, Dpp6,
                                            "samsung,exynos9-dpp"))
    return EFI_COMPROMISED_DATA;
  Dpp6Reg = fdt_getprop(Destination, Dpp6, "reg", &Length);
  if (!Dpp6Reg || Length != sizeof(Dpp6ExpectedReg) ||
      CompareMem(Dpp6Reg, Dpp6ExpectedReg, sizeof(Dpp6ExpectedReg)))
    return EFI_COMPROMISED_DATA;
  Dpp6Attr = fdt_getprop(Destination, Dpp6, "attr", &Length);
  if (!Dpp6Attr || Length != sizeof(fdt32_t) ||
      fdt32_to_cpu(*Dpp6Attr) != 0x000A0010U)
    return EFI_COMPROMISED_DATA;
  Dpp6Iommus = fdt_getprop(Destination, Dpp6, "iommus", &Length);
  if (!Dpp6Iommus || Length != sizeof(Dpp6ExpectedIommus) ||
      CompareMem(Dpp6Iommus, Dpp6ExpectedIommus,
                 sizeof(Dpp6ExpectedIommus)))
    return EFI_COMPROMISED_DATA;
  Dpp6Status = fdt_getprop(Destination, Dpp6, "status", &Length);
  if (!Dpp6Status || Length != sizeof("disabled") ||
      CompareMem(Dpp6Status, "disabled", sizeof("disabled")))
    return EFI_COMPROMISED_DATA;
  if (EFI_ERROR(CheckFbHandoverFdt(Destination)))
    return EFI_COMPROMISED_DATA;
  *UsedBytes = (UINTN)fdt_totalsize(Destination);
  if (*UsedBytes > B2R_FDT_SPAN ||
      !CheckLoaderMemreserve(Destination, TRUE, Manifest->StreamEnd))
    return EFI_COMPROMISED_DATA;
  Chosen = fdt_path_offset(Destination, "/chosen");
  Start = fdt_getprop(Destination, Chosen, "linux,initrd-start", &Length);
  if (!Start || Length != 8) return EFI_COMPROMISED_DATA;
  CopyMem(&StartValue, Start, 8);
  End = fdt_getprop(Destination, Chosen, "linux,initrd-end", &Length);
  if (!End || Length != 8) return EFI_COMPROMISED_DATA;
  CopyMem(&EndValue, End, 8);
  if (fdt64_to_cpu(StartValue) != B2R_RAMDISK_DEST ||
      fdt64_to_cpu(EndValue) !=
        B2R_RAMDISK_DEST + Manifest->DiagRamdiskSize)
    return EFI_COMPROMISED_DATA;
  Ect = fdt_path_offset(Destination, "/ect");
  if (Ect < 0) return EFI_COMPROMISED_DATA;
  EctAddress = fdt_getprop(Destination, Ect, "parameter_address", &Length);
  if (!EctAddress || Length != sizeof(fdt32_t) ||
      fdt32_to_cpu(*EctAddress) != (UINT32)B2R_ECT_DEST)
    return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HandoffLinux(EFI_HANDLE ImageHandle,
                               CONST B2R_MANIFEST *Manifest,
                               CONST struct b2r_boot_image *Boot) {
  EFI_STATUS Status;
  EFI_PHYSICAL_ADDRESS Entry = B2R_KERNEL_DEST;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  UINTN MapCapacity = 0x40000, MapBytes, Key, Stride;
  UINT32 Version;
  UINTN FdtBytes = 0, Attempt;
  UINT64 Stack;
  BOOLEAN KernelOwned = FALSE, RamdiskOwned = FALSE, FdtOwned = FALSE;
  BOOLEAN EctOwned = FALSE;
  B2R_CLEAN_RANGE Ranges[6];
  CONST UINT8 *Image = (CONST UINT8 *)(UINTN)Manifest->ImagePa;
  CONST UINT8 *Diag = (CONST UINT8 *)(UINTN)Manifest->DiagRamdiskPa;
  Status = ValidateArchTimer(Manifest);
  if (EFI_ERROR(Status)) return Status;
  if (Manifest->DiagRamdiskSize > B2R_RAMDISK_SPAN ||
      Boot->kernel.size > B2R_KERNEL_SPAN -
                            (B2R_KERNEL_DEST - B2R_KERNEL_BASE))
    return EFI_UNSUPPORTED;
  Status = AllocateExact(B2R_KERNEL_BASE, B2R_KERNEL_SPAN);
  if (EFI_ERROR(Status)) return Status;
  KernelOwned = TRUE;
  Status = AllocateExact(B2R_RAMDISK_DEST, B2R_RAMDISK_SPAN);
  if (EFI_ERROR(Status)) goto Fail;
  RamdiskOwned = TRUE;
  Status = AllocateExact(B2R_FDT_DEST, B2R_FDT_SPAN);
  if (EFI_ERROR(Status)) goto Fail;
  FdtOwned = TRUE;
  Status = AllocateExact(B2R_ECT_DEST, B2R_ECT_SPAN);
  if (EFI_ERROR(Status)) goto Fail;
  EctOwned = TRUE;
  Mark(104, 0x986B5603); /* All four physical ranges owned by this app. */
  /* The addresses below are physical constants cast to pointers. Prove
   * identity and write permission before touching a destination byte. */
  if (!IdentityRange(B2R_KERNEL_BASE, B2R_KERNEL_SPAN, TRUE) ||
      !IdentityRange(B2R_RAMDISK_DEST, B2R_RAMDISK_SPAN, TRUE) ||
      !IdentityRange(B2R_FDT_DEST, B2R_FDT_SPAN, TRUE) ||
      !IdentityRange(B2R_ECT_DEST, B2R_ECT_SPAN, TRUE)) {
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }
  Mark(104, 0x986B5604); /* Destination VA=PA and writable. */
  ZeroMem((VOID *)(UINTN)B2R_KERNEL_BASE, (UINTN)B2R_KERNEL_SPAN);
  ZeroMem((VOID *)(UINTN)B2R_RAMDISK_DEST, (UINTN)B2R_RAMDISK_SPAN);
  ZeroMem((VOID *)(UINTN)B2R_FDT_DEST, (UINTN)B2R_FDT_SPAN);
  CopyMem((VOID *)(UINTN)B2R_KERNEL_DEST,
          Image + Boot->kernel.offset, (UINTN)Boot->kernel.size);
  CopyMem((VOID *)(UINTN)B2R_RAMDISK_DEST, Diag,
          (UINTN)Manifest->DiagRamdiskSize);
  Status = CopyAndVerifyOriginalEct();
  if (EFI_ERROR(Status)) goto Fail;
  Status = PrepareLinuxFdt(Manifest, &FdtBytes);
  if (EFI_ERROR(Status)) goto Fail;
  Mark(104, 0x986B5605); /* Kernel/initrd copied; chosen FDT packed. */
  if (fdt_path_offset((VOID *)(UINTN)B2R_FDT_DEST, "/chosen") < 0 ||
      !StorageNodeDisabled((VOID *)(UINTN)B2R_FDT_DEST,
                           "/ufs@0x13100000") ||
      !StorageNodeDisabled((VOID *)(UINTN)B2R_FDT_DEST, "/ufs-srpmb") ||
      !StorageNodeDisabled((VOID *)(UINTN)B2R_FDT_DEST,
                           "/dwmmc2@132E0000")) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                               (VOID **)&Loaded);
  if (EFI_ERROR(Status)) goto Fail;
  if (!Loaded) { Status = EFI_COMPROMISED_DATA; goto Fail; }
  __asm__ volatile("mov %0, sp" : "=r"(Stack));
  if (Stack < 16384 ||
      !IdentityRange(B2R_KERNEL_BASE, B2R_KERNEL_SPAN, TRUE) ||
      !IdentityRange(B2R_RAMDISK_DEST, B2R_RAMDISK_SPAN, TRUE) ||
      !IdentityRange(B2R_FDT_DEST, B2R_FDT_SPAN, TRUE) ||
      !IdentityRange(B2R_ECT_DEST, B2R_ECT_SPAN, TRUE) ||
      !IdentityRange((UINT64)(UINTN)Loaded->ImageBase,
                     Loaded->ImageSize, FALSE) ||
      !IdentityRange(Stack - 16384, 20480, TRUE) ||
      !IdentityRange(0x95E00000ULL, 0x1000, TRUE)) {
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }
  Mark(104, 0x986B5606); /* Final EL1 identity mapping checked. */
  Ranges[0] = (B2R_CLEAN_RANGE){B2R_KERNEL_BASE,
                                B2R_KERNEL_BASE + B2R_KERNEL_SPAN};
  Ranges[1] = (B2R_CLEAN_RANGE){B2R_RAMDISK_DEST,
                                B2R_RAMDISK_DEST + B2R_RAMDISK_SPAN};
  Ranges[2] = (B2R_CLEAN_RANGE){B2R_FDT_DEST,
                                B2R_FDT_DEST + B2R_FDT_SPAN};
  Ranges[3] = (B2R_CLEAN_RANGE){B2R_ECT_DEST,
                                B2R_ECT_DEST + B2R_ECT_SPAN};
  Ranges[4] = (B2R_CLEAN_RANGE){(UINT64)(UINTN)Loaded->ImageBase,
                                (UINT64)(UINTN)Loaded->ImageBase +
                                  Loaded->ImageSize};
  Ranges[5] = (B2R_CLEAN_RANGE){Stack - 16384, Stack + 4096};
#if N986B_RECOVERY_ECT_HOLD
  Status = gBS->SetWatchdogTimer(0, 0, 0, NULL);
  if (EFI_ERROR(Status)) goto Fail;
  Mark(128, 0x986B5802ULL);
  Print(L"===== N986B ECT RELOCATION PREFLIGHT PASSED; HOLD FOR UPLOAD =====\r\n");
  CpuDeadLoop();
#else
  Status = gBS->SetWatchdogTimer(0, 0, 0, NULL);
  if (EFI_ERROR(Status)) goto Fail;
  Map = AllocatePool(MapCapacity);
  if (!Map) { Status = EFI_OUT_OF_RESOURCES; goto Fail; }
  Mark(96, 0x986B5401); /* Exact EFI ownership and identity proved. */
  for (Attempt = 0; Attempt < 8; ++Attempt) {
    MapBytes = MapCapacity;
    Status = gBS->GetMemoryMap(&MapBytes, Map, &Key, &Stride, &Version);
    if (EFI_ERROR(Status)) break;
    if (!RangeInType(Map, MapBytes, Stride, B2R_MANIFEST_PA,
                     Manifest->StreamEnd - B2R_MANIFEST_PA,
                     EfiReservedMemoryType) ||
        !RangeInType(Map, MapBytes, Stride, B2R_KERNEL_BASE,
                     B2R_KERNEL_SPAN, EfiLoaderData) ||
        !RangeInType(Map, MapBytes, Stride, B2R_RAMDISK_DEST,
                     B2R_RAMDISK_SPAN, EfiLoaderData) ||
        !RangeInType(Map, MapBytes, Stride, B2R_FDT_DEST,
                     B2R_FDT_SPAN, EfiLoaderData) ||
        !RangeInType(Map, MapBytes, Stride, B2R_ECT_DEST,
                      B2R_ECT_SPAN, EfiLoaderData) ||
        !RangeInType(Map, MapBytes, Stride, 0x95E00000ULL,
                     0x1000, EfiReservedMemoryType)) {
      Status = EFI_COMPROMISED_DATA;
      break;
    }
    Mark(104, 0x986B5607); /* Final EFI ownership checked. */
    Status = gBS->ExitBootServices(ImageHandle, Key);
    if (!EFI_ERROR(Status)) {
      Mark(104, 0x986B5608); /* ExitBootServices returned success. */
      Mark(96, 0x986B5402);
      B2RLinuxHandoff(Entry, B2R_FDT_DEST, Ranges);
      CpuDeadLoop();
    }
    if (Status != EFI_INVALID_PARAMETER) break;
  }
  if (!EFI_ERROR(Status)) Status = EFI_COMPROMISED_DATA;
#endif
Fail:
  Mark(96, (UINT64)Status);
  if (Map) FreePool(Map);
  if (FdtOwned) gBS->FreePages(B2R_FDT_DEST,
                               (UINTN)(B2R_FDT_SPAN >> EFI_PAGE_SHIFT));
  if (EctOwned) gBS->FreePages(B2R_ECT_DEST,
                               (UINTN)(B2R_ECT_SPAN >> EFI_PAGE_SHIFT));
  if (RamdiskOwned) gBS->FreePages(B2R_RAMDISK_DEST,
                                   (UINTN)(B2R_RAMDISK_SPAN >> EFI_PAGE_SHIFT));
  if (KernelOwned) gBS->FreePages(B2R_KERNEL_BASE,
                                  (UINTN)(B2R_KERNEL_SPAN >> EFI_PAGE_SHIFT));
  (VOID)FdtBytes;
  return Status;
}
#endif

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;
  B2R_MANIFEST Manifest;
  struct b2r_boot_image Boot;
  UINT64 CurrentEl, Mpidr;
  (VOID)ImageHandle; (VOID)SystemTable;
  ZeroMem(&Manifest, sizeof(Manifest));
  ZeroMem(&Boot, sizeof(Boot));
  /* Remove stale warm-reset markers before the first current-run stage. */
  ZeroMem((VOID *)(UINTN)B2R_MARKER_PA, 144);
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)B2R_MARKER_PA, 144);
#if N986B_RECOVERY_HOLD
  ZeroMem((VOID *)(UINTN)B2R_HOLD_MARKER_PA, 128);
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)B2R_HOLD_MARKER_PA, 128);
  ZeroMem((VOID *)(UINTN)B2R_HOLD_STREAM_MARKER_PA, 128);
  WriteBackInvalidateDataCacheRange((VOID *)(UINTN)B2R_HOLD_STREAM_MARKER_PA, 128);
#endif
  Mark(0, 0x986B5300);
  __asm__ volatile("mrs %0, CurrentEL" : "=r"(CurrentEl));
  __asm__ volatile("mrs %0, mpidr_el1" : "=r"(Mpidr));
#if N986B_RECOVERY_ENABLE_HANDOFF
#if N986B_RECOVERY_UI_MODE
  Print(L"N986B isolated RAM TWRP UI handoff candidate\r\n");
#else
  Print(L"N986B read-only Linux diagnostic handoff candidate\r\n");
#endif
#else
#if N986B_RECOVERY_HOLD
  Print(L"N986B recovery RAM preflight; hold after result for Upload A/B\r\n");
#else
  Print(L"N986B TWRP RAM diagnostic; no kernel handoff, no storage I/O\r\n");
#endif
#endif
  if ((CurrentEl >> 2) != 1 || (Mpidr & 0x000000FF00FFFFFFULL)) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  Status = CheckStream(&Manifest, &Boot);
  if (EFI_ERROR(Status)) goto Done;
  Mark(40, 0x986B5305);
  Status = CheckMemoryMap(Manifest.StreamEnd);
  if (EFI_ERROR(Status)) goto Done;
#if N986B_RECOVERY_RELOCATED || N986B_RECOVERY_UI_HIGH_STREAM
  Status = CheckSecDebugCanary();
  if (EFI_ERROR(Status)) goto Done;
#endif
  Mark(48, 0x986B5306);
  Print(L"boot v2; kernel=%lu diagnostic initrd=%lu; merged DTB=%lu; overlay=4\r\n",
        Boot.kernel.size, Manifest.DiagRamdiskSize, Manifest.MergedFdtSize);
  Print(L"header address hints 0x%x/0x%x ignored; candidate Linux destinations"
        L" 0x%lx/0x%lx/0x%lx conventional now, not owned\r\n",
        Boot.kernel_addr_hint, Boot.ramdisk_addr_hint,
        B2R_KERNEL_DEST, B2R_RAMDISK_DEST, B2R_FDT_DEST);
#if N986B_RECOVERY_ENABLE_HANDOFF
  Print(L"reserved stream and low destinations checked;"
        L" claiming exact EFI pages for Linux handoff\r\n");
#else
  Print(L"reserved stream and low destinations checked;"
        L" no copy, allocation, EBS or branch\r\n");
#endif
  Mark(56, 0x986B53FF);
#if N986B_RECOVERY_ENABLE_HANDOFF
  Status = HandoffLinux(ImageHandle, &Manifest, &Boot);
#endif
Done:
#if N986B_RECOVERY_HOLD
  /* StartImage armed BDS's watchdog. Disable it before the intentional hold. */
  {
    EFI_STATUS WatchdogStatus = gBS->SetWatchdogTimer(0, 0, 0, NULL);
    if (EFI_ERROR(WatchdogStatus)) {
      Print(L"===== HOLD WATCHDOG DISABLE FAILED: %r =====\r\n", WatchdogStatus);
      if (!EFI_ERROR(Status)) Status = WatchdogStatus;
    }
  }
#endif
  Mark(64, (UINT64)Status);
  if (EFI_ERROR(Status))
    Print(L"===== N986B RECOVERY PREFLIGHT FAILED: %r =====\r\n", Status);
  else
    Print(L"===== N986B RECOVERY PREFLIGHT PASSED =====\r\n");
#if N986B_RECOVERY_HOLD
  Mark(96, 0x986B55AAULL);
  Print(L"===== HOLDING HERE FOR RAM UPLOAD; NO SHELL =====\r\n");
  CpuDeadLoop();
#endif
  return Status;
}
