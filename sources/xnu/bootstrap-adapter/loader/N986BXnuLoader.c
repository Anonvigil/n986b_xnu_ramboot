/** @file
  Isolated RAM-only N986B iOS 16.5/XNU 8796 loader adapter.

  The SHA-256-checked USB stream ABI is identical to N986BRamPreflight. This
  adapter keeps the physical ARM64 branch compile-time gated while its
  XNU 8796 memory-map and page-granule contracts are validated on the host.

  No flash, partition, UEFI variable or filesystem access is performed.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <libfdt.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Arena.h"
#include "BootArgs.h"
#include "DeviceTree.h"
#include "MachO.h"
#include "Progress.h"
#include "Xnu8796Layout.h"

#define N986B_MANIFEST_BASE  0x90700000ULL
#define N986B_KERNEL_BASE    0x90701000ULL
#define N986B_STREAM_LIMIT   0x95D00000ULL
#define N986B_MANIFEST_SIZE  0x1000U
#define N986B_STREAM_ALIGN   0x10000ULL
#define N986B_KERNEL_MAX     (128ULL * 1024ULL * 1024ULL)
#define N986B_RAMDISK_MAX    (64ULL * 1024ULL * 1024ULL)
#define N986B_MAP_SPACE      (1024U * 1024U)
#define N986B_MARKER_BASE    0x95E00000ULL
#define N986B_MARK_MPIDR     0x840U
#define N986B_MARK_MIDR      0x848U
#define N986B_MARK_CNTFRQ    0x850U
#define N986B_MARK_EL        0x858U
// Cache-cleaned Upload readback fields in the reserved 0x95E00000 page.
#define N986B_MARK_STAGE     0x860U
#define N986B_MARK_STATUS    0x868U
#define N986B_MARK_PARSE     0x870U
#define N986B_MARK_ENTRY_PA  0x878U
#define N986B_MARK_ARGS_PA   0x880U
#define N986B_MARK_DT_PA     0x888U
#define N986B_MARK_ARENA    0x890U
#define N986B_MARK_RETURN   0x898U
#define N986B_MARK_LOAD_BASE 0x8A0U
#define N986B_MARK_LOAD_SIZE 0x8A8U
#define N986B_MARK_CONGRUENCE 0x8B0U
#define N986B_MARK_EFFECTIVE_CNTFRQ 0x8B8U
#define N986B_MARK_FDT_STAGE 0x8C0U
#define N986B_MARK_FDT_SIZE 0x8C8U
#define N986B_MARK_CNTVCT_START 0x8D0U
#define N986B_MARK_CNTVCT_END 0x8D8U
#define N986B_MARK_CNTVCT_DELTA 0x8E0U
#define N986B_MARK_CNTVCT_LIVE 0x8E8U
#define N986B_MARK_MANIFEST_SHA 0x8F0U // Four cache-cleaned UINT64 words.
#define N986B_MARK_STAGE11_TAG 0x910U
#define N986B_LIVE_FDT_BASE 0x8A000000ULL
#define N986B_LIVE_FDT_MAX  0x100000U
#define N986B_TIMER_HZ     26000000U
#define N986B_SNAPSHOT_BASE N986B_STREAM_LIMIT
#define N986B_SNAPSHOT_PAGE_SIZE 0x1000U
#define N986B_SNAPSHOT_RESERVED_END 0x95D01000ULL
#define N986B_SNAPSHOT_STAGE_IDENTITY 0x10ULL
#define N986B_SNAPSHOT_STAGE_PREFLIGHT 0x11ULL
#define N986B_SNAPSHOT_STAGE_EBS 0x20ULL
#define N986B_SNAPSHOT_STAGE_PREBRANCH 0x30ULL
#define N986B_SNAPSHOT_STAGE_FAILED 0xE0ULL

#ifndef N986B_ENABLE_XNU_HANDOFF
#define N986B_ENABLE_XNU_HANDOFF 0
#endif
#if N986B_ENABLE_XNU_HANDOFF != 0 && N986B_ENABLE_XNU_HANDOFF != 1
#error N986B_ENABLE_XNU_HANDOFF must be zero or one
#endif
#if N986B_ENABLE_XNU_HANDOFF
#error XNU8796 handoff blocked pending GIC, CPU0 clocks, memory-map, and boot_args validation
#endif

#pragma pack (push, 1)
typedef struct {
  UINT8   Magic[8];
  UINT32  Version;
  UINT32  HeaderSize;
  UINT64  KernelPa;
  UINT64  KernelSize;
  UINT64  StreamEnd;
  UINT8   KernelSha256[32];
  UINT8   RandomSeed[256];
  UINT32  Flags;
  UINT64  RamdiskPa;
  UINT64  RamdiskSize;
  UINT8   RamdiskSha256[32];
  UINT8   Reserved[N986B_MANIFEST_SIZE - 380];
} N986B_RAM_MANIFEST;

typedef struct {
  UINT8 Magic[8];
  UINT32 Version;
  UINT32 SnapshotSize;
  UINT64 Stage;
  UINT64 Status;
  UINT64 BootArgsPa;
  UINT64 DeviceTreePa;
  UINT32 DeviceTreeSize;
  UINT32 Reserved;
  UINT8 DeviceTreeSha256[32];
  UINT8 BootArgs[sizeof (struct b2l_boot_args)];
  UINT8 ManifestSha256[32];
} N986B_HANDOFF_SNAPSHOT;
#pragma pack (pop)

STATIC_ASSERT (sizeof (N986B_RAM_MANIFEST) == N986B_MANIFEST_SIZE, "manifest ABI");
STATIC_ASSERT (sizeof (N986B_HANDOFF_SNAPSHOT) <= N986B_SNAPSHOT_PAGE_SIZE,
               "snapshot fits reserved page");
STATIC_ASSERT (sizeof (N986B_HANDOFF_SNAPSHOT) == 856,
               "XNU 8796 snapshot ABI size");
STATIC_ASSERT (sizeof (N986B_HANDOFF_SNAPSHOT) < 0x500,
               "snapshot leaves XNU witness at +0x500 free");
STATIC_ASSERT (N986B_SNAPSHOT_BASE + N986B_SNAPSHOT_PAGE_SIZE ==
               N986B_SNAPSHOT_RESERVED_END, "snapshot page bound");
STATIC_ASSERT (OFFSET_OF (N986B_HANDOFF_SNAPSHOT, Stage) == 16 &&
               OFFSET_OF (N986B_HANDOFF_SNAPSHOT, Status) == 24,
               "snapshot stage/status offsets");
STATIC_ASSERT (OFFSET_OF (N986B_HANDOFF_SNAPSHOT, BootArgs) == 88,
               "snapshot boot_args offset");
STATIC_ASSERT (OFFSET_OF (N986B_HANDOFF_SNAPSHOT, ManifestSha256) == 824,
               "XNU 8796 snapshot manifest digest offset");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, RandomSeed) == 72, "seed ABI");
STATIC_ASSERT (OFFSET_OF (EFI_MEMORY_DESCRIPTOR, PhysicalStart) == 8, "EFI map ABI");
STATIC_ASSERT (OFFSET_OF (EFI_MEMORY_DESCRIPTOR, NumberOfPages) == 24, "EFI map ABI");
STATIC_ASSERT (EfiConventionalMemory == 7 && EfiLoaderData == 2 &&
               EfiReservedMemoryType == 0, "EFI type ABI");

STATIC CONST UINT8 mMagic[8] = { 'N', '9', '8', '6', 'B', 'X', 'N', 'U' };

VOID EFIAPI B2LXnuHandoff (
  UINT64 EntryPhysical,
  UINT64 BootArgsPhysical,
  CONST struct b2l_handoff_info *Info
  );

STATIC
EFI_STATUS
VerifyStream (
  OUT N986B_RAM_MANIFEST *Header
  )
{
  CONST UINT8 *Kernel;
  UINT8 Digest[32];
  UINTN Index;
  UINT8 SeedOr = 0;

  InvalidateDataCacheRange ((VOID *)(UINTN)N986B_MANIFEST_BASE,
                            N986B_MANIFEST_SIZE);
  CopyMem (Header, (CONST VOID *)(UINTN)N986B_MANIFEST_BASE, sizeof (*Header));
  if (CompareMem (Header->Magic, mMagic, sizeof (mMagic)) != 0)
    return EFI_NOT_FOUND;
  if ((Header->Version != 1 && Header->Version != 2) ||
      Header->HeaderSize != N986B_MANIFEST_SIZE ||
      Header->KernelPa != N986B_KERNEL_BASE ||
      Header->Flags != (Header->Version == 2 ? 1U : 0U))
    return EFI_COMPROMISED_DATA;
  if (Header->StreamEnd <= N986B_KERNEL_BASE ||
      Header->StreamEnd > N986B_STREAM_LIMIT ||
      (Header->StreamEnd & (N986B_STREAM_ALIGN - 1)) != 0 ||
      Header->KernelSize < 4096 || Header->KernelSize > N986B_KERNEL_MAX ||
      Header->KernelSize > Header->StreamEnd - N986B_KERNEL_BASE)
    return EFI_BAD_BUFFER_SIZE;
  for (Index = 0; Index < sizeof (Header->Reserved); ++Index)
    if (Header->Reserved[Index] != 0) return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < sizeof (Header->RandomSeed); ++Index)
    SeedOr |= Header->RandomSeed[Index];
  if (!SeedOr) return EFI_SECURITY_VIOLATION;

  Kernel = (CONST UINT8 *)(UINTN)Header->KernelPa;
  InvalidateDataCacheRange ((VOID *)(UINTN)Header->KernelPa,
                            (UINTN)(Header->StreamEnd - Header->KernelPa));
  if (*(CONST UINT32 *)Kernel != 0xFEEDFACF ||
      *(CONST UINT32 *)(Kernel + 4) != 0x0100000C)
    return EFI_LOAD_ERROR;
  if (!Sha256HashAll (Kernel, (UINTN)Header->KernelSize, Digest))
    return EFI_DEVICE_ERROR;
  if (CompareMem (Digest, Header->KernelSha256, sizeof (Digest)) != 0) {
    ZeroMem (Digest, sizeof (Digest));
    return EFI_COMPROMISED_DATA;
  }
  ZeroMem (Digest, sizeof (Digest));
  if (Header->Version == 2) {
    CONST UINT8 *Ramdisk;
    if (Header->RamdiskPa < Header->KernelPa + Header->KernelSize ||
        (Header->RamdiskPa & (N986B_XNU_PAGE_SIZE - 1)) != 0 ||
        Header->RamdiskSize < N986B_XNU_PAGE_SIZE ||
        Header->RamdiskSize > N986B_RAMDISK_MAX ||
        (Header->RamdiskSize & (N986B_XNU_PAGE_SIZE - 1)) != 0 ||
        Header->RamdiskPa > Header->StreamEnd ||
        Header->RamdiskSize > Header->StreamEnd - Header->RamdiskPa)
      return EFI_BAD_BUFFER_SIZE;
    for (Index = (UINTN)Header->KernelSize;
         Index < (UINTN)(Header->RamdiskPa - Header->KernelPa); ++Index)
      if (Kernel[Index] != 0) return EFI_COMPROMISED_DATA;
    Ramdisk = (CONST UINT8 *)(UINTN)Header->RamdiskPa;
    if (Ramdisk[1024] != 'H' ||
        (Ramdisk[1025] != '+' && Ramdisk[1025] != 'X'))
      return EFI_LOAD_ERROR;
    if (!Sha256HashAll (Ramdisk, (UINTN)Header->RamdiskSize, Digest))
      return EFI_DEVICE_ERROR;
    if (CompareMem (Digest, Header->RamdiskSha256, sizeof (Digest)) != 0) {
      ZeroMem (Digest, sizeof (Digest));
      return EFI_COMPROMISED_DATA;
    }
    ZeroMem (Digest, sizeof (Digest));
    for (Index = (UINTN)(Header->RamdiskPa + Header->RamdiskSize -
                         Header->KernelPa);
         Index < (UINTN)(Header->StreamEnd - Header->KernelPa); ++Index)
      if (Kernel[Index] != 0) return EFI_COMPROMISED_DATA;
  } else {
    if (Header->RamdiskPa != 0 || Header->RamdiskSize != 0) return EFI_COMPROMISED_DATA;
    for (Index = 0; Index < sizeof (Header->RamdiskSha256); ++Index)
      if (Header->RamdiskSha256[Index] != 0) return EFI_COMPROMISED_DATA;
    for (Index = (UINTN)Header->KernelSize;
         Index < (UINTN)(Header->StreamEnd - Header->KernelPa); ++Index)
      if (Kernel[Index] != 0) return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC UINT64 ReadCurrentEl (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, CurrentEL" : "=r" (V)); return V >> 2; }
STATIC UINT64 ReadMpidr (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (V)); return V; }
STATIC UINT64 ReadMidr (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, midr_el1" : "=r" (V)); return V; }
STATIC UINT32 ReadCounterFrequency (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (V)); return (UINT32)V; }

STATIC VOID MarkRegister (UINTN Offset, UINT64 Value)
{
  volatile UINT64 *Slot = (volatile UINT64 *)(UINTN)(N986B_MARKER_BASE + Offset);
  *Slot = Value;
  WriteBackDataCacheRange ((VOID *)(UINTN)Slot, sizeof (*Slot));
  MemoryFence ();
}

// Cache maintenance here uses AArch64 range instructions, not Boot Services.
// It remains callable for the success markers after ExitBootServices.
STATIC VOID MarkHandoffSnapshot (
  IN volatile N986B_HANDOFF_SNAPSHOT *Snapshot,
  IN UINT64 Stage,
  IN EFI_STATUS Status
  )
{
  Snapshot->Stage = Stage;
  Snapshot->Status = (UINT64)Status;
  MemoryFence ();
  WriteBackDataCacheRange ((VOID *)(UINTN)N986B_SNAPSHOT_BASE,
                           N986B_SNAPSHOT_PAGE_SIZE);
  MemoryFence ();
}

// Accept a zero CNTFRQ_EL0 only when the reserved, live N986B FDT advertises
// the generic timer at exactly 26 MHz. The tree may change between boots.
STATIC EFI_STATUS ValidateLiveFdtTimer (OUT UINT32 *FdtSize)
{
  CONST VOID *Fdt = (CONST VOID *)(UINTN)N986B_LIVE_FDT_BASE;
  CONST fdt32_t *Clock;
  UINT32 Total, StructOffset, StructSize, StringsOffset, StringsSize, ClockRaw;
  INT32 Node, Length;
  UINT64 Pointer = *(volatile CONST UINT64 *)(UINTN)N986B_MARKER_BASE;

  *FdtSize = 0;
  if (Pointer != N986B_LIVE_FDT_BASE) return EFI_NOT_FOUND;
  MarkRegister (N986B_MARK_FDT_STAGE, 1); // Live FDT pointer matched.
  InvalidateDataCacheRange ((VOID *)(UINTN)Fdt, N986B_LIVE_FDT_MAX);
  if (fdt_check_header (Fdt) != 0) return EFI_COMPROMISED_DATA;
  Total = fdt_totalsize (Fdt);
  StructOffset = fdt_off_dt_struct (Fdt);
  StructSize = fdt_size_dt_struct (Fdt);
  StringsOffset = fdt_off_dt_strings (Fdt);
  StringsSize = fdt_size_dt_strings (Fdt);
  if (Total < sizeof (struct fdt_header) || Total > N986B_LIVE_FDT_MAX ||
      StructOffset > Total || StructSize > Total - StructOffset ||
      StringsOffset > Total || StringsSize > Total - StringsOffset)
    return EFI_COMPROMISED_DATA;
  *FdtSize = Total;
  MarkRegister (N986B_MARK_FDT_SIZE, Total);
  MarkRegister (N986B_MARK_FDT_STAGE, 2); // Bounded FDT header valid.
  Node = fdt_path_offset (Fdt, "/timer");
  if (Node < 0) return EFI_NOT_FOUND;
  MarkRegister (N986B_MARK_FDT_STAGE, 3); // /timer exists.
  if (fdt_node_check_compatible (Fdt, Node, "arm,armv8-timer") != 0)
    return EFI_UNSUPPORTED;
  MarkRegister (N986B_MARK_FDT_STAGE, 4); // Generic timer compatible.
  Clock = fdt_getprop (Fdt, Node, "clock-frequency", &Length);
  if (Clock == NULL || Length != sizeof (ClockRaw))
    return EFI_COMPROMISED_DATA;
  CopyMem (&ClockRaw, Clock, sizeof (ClockRaw));
  if (fdt32_to_cpu (ClockRaw) != N986B_TIMER_HZ)
    return EFI_UNSUPPORTED;
  MarkRegister (N986B_MARK_FDT_STAGE, 0x986B0026); // 26 MHz timer validated.
  return EFI_SUCCESS;
}

STATIC UINT64 ReadVirtualCounter (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, cntvct_el0" : "=r" (V)); return V; }
STATIC UINT64 ReadSctlr (VOID)
{ UINT64 V; __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (V)); return V; }

STATIC BOOLEAN IdentityAddress (UINT64 Address)
{
  UINT64 Par;
  if (!(ReadSctlr () & 1)) return TRUE;
  __asm__ volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1"
                    : "=r" (Par) : "r" (Address) : "memory");
  if (Par & 1) return FALSE;
  return (Par & 0x0000FFFFFFFFF000ULL) == (Address & 0x0000FFFFFFFFF000ULL);
}

STATIC BOOLEAN IdentityRange (UINT64 Address, UINT64 Length)
{
  UINT64 End;
  if (!Length || Address > MAX_UINT64 - (Length - 1)) return FALSE;
  End = Address + Length - 1;
  if (!IdentityAddress (Address) || !IdentityAddress (End)) return FALSE;
  for (Address &= ~4095ULL; Address < End;) {
    if (!IdentityAddress (Address)) return FALSE;
    if (End - Address < 4096) break;
    Address += 4096;
  }
  return TRUE;
}

// The snapshot and GOP progress squares are written both before and after
// ExitBootServices. Reject any EL1 write translation fault or PA alias.
STATIC BOOLEAN IdentityWriteAddress (UINT64 Address)
{
  UINT64 Par;
  if (!(ReadSctlr () & 1)) return FALSE; // No AT write-permission proof.
  __asm__ volatile ("at s1e1w, %1\n\tisb\n\tmrs %0, par_el1"
                    : "=r" (Par) : "r" (Address) : "memory");
  if (Par & 1) return FALSE;
  return (Par & 0x0000FFFFFFFFF000ULL) ==
         (Address & 0x0000FFFFFFFFF000ULL);
}

STATIC BOOLEAN IdentityWriteRange (UINT64 Address, UINT64 Length)
{
  UINT64 End;
  if (!Length || Address > MAX_UINT64 - (Length - 1)) return FALSE;
  End = Address + Length - 1;
  if (!IdentityWriteAddress (Address) || !IdentityWriteAddress (End))
    return FALSE;
  for (Address &= ~4095ULL; Address < End;) {
    if (!IdentityWriteAddress (Address)) return FALSE;
    if (End - Address < 4096) break;
    Address += 4096;
  }
  return TRUE;
}

STATIC EFI_STATUS GetVideo (OUT struct b2l_boot_video *Video)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_STATUS Status;
  UINT64 Needed;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL,
                                (VOID **)&Gop);
  if (EFI_ERROR (Status)) return Status;
  if (!Gop->Mode || !Gop->Mode->Info ||
      Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
    return EFI_UNSUPPORTED;
  Video->base = Gop->Mode->FrameBufferBase;
  Video->display = 0;
  Video->width = Gop->Mode->Info->HorizontalResolution;
  Video->height = Gop->Mode->Info->VerticalResolution;
  Video->row_bytes = (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4;
  Video->depth = 32;
  if (Video->base != 0xFA000000ULL || Video->width != 1440 ||
      Video->height != 3088 ||
      Gop->Mode->Info->PixelsPerScanLine < Video->width ||
      Video->row_bytes > MAX_UINT64 / Video->height)
    return EFI_UNSUPPORTED;
  Needed = Video->row_bytes * Video->height;
  if (Needed > Gop->Mode->FrameBufferSize || Needed > 0x1400000ULL)
    return EFI_BAD_BUFFER_SIZE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SnapshotMap (
  IN VOID *Map,
  OUT UINTN *MapSize,
  OUT UINTN *MapKey,
  OUT UINTN *DescriptorSize,
  OUT UINT32 *DescriptorVersion
  )
{
  *MapSize = N986B_MAP_SPACE;
  return gBS->GetMemoryMap (MapSize, Map, MapKey,
                            DescriptorSize, DescriptorVersion);
}

STATIC BOOLEAN SnapshotPageIsReserved (
  IN CONST VOID *Map,
  IN UINTN MapSize,
  IN UINTN Stride
  )
{
  BOOLEAN Covered = FALSE;
  if (Map == NULL || Stride < sizeof (EFI_MEMORY_DESCRIPTOR) ||
      MapSize == 0 || MapSize % Stride != 0)
    return FALSE;
  for (UINTN Offset = 0; Offset < MapSize; Offset += Stride) {
    CONST EFI_MEMORY_DESCRIPTOR *D =
        (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UINT8 *)Map + Offset);
    UINT64 Start = D->PhysicalStart;
    UINT64 End;
    if (D->NumberOfPages == 0 ||
        D->NumberOfPages > (MAX_UINT64 - Start) / EFI_PAGE_SIZE)
      return FALSE;
    End = Start + D->NumberOfPages * EFI_PAGE_SIZE;
    if (End <= N986B_SNAPSHOT_BASE || Start >= N986B_SNAPSHOT_RESERVED_END)
      continue;
    if (Covered || D->Type != EfiReservedMemoryType ||
        Start > N986B_SNAPSHOT_BASE || End < N986B_SNAPSHOT_RESERVED_END)
      return FALSE;
    Covered = TRUE;
  }
  return Covered;
}

STATIC
EFI_STATUS
ReserveArena (
  IN VOID *Map,
  OUT struct n986b_arena_chunk Chunks[N986B_MAX_CHUNKS],
  OUT unsigned *ChunkCount,
  OUT UINT64 *Base,
  OUT UINT64 *Size
  )
{
  UINTN MapSize, Key, Stride;
  UINT32 Version;
  UINT64 CandidateBase, CandidateSize;
  unsigned Planned = 0;
  EFI_STATUS Status = SnapshotMap (Map, &MapSize, &Key, &Stride, &Version);
  if (EFI_ERROR (Status)) return Status;
  if (n986b_choose_arena (Map, MapSize, Stride, &CandidateBase,
                          &CandidateSize) != 0 ||
      CandidateBase != N986B_RAM_START ||
      CandidateSize != N986B_MIN_ARENA ||
      n986b_arena_chunks (Map, MapSize, Stride, CandidateBase, CandidateSize,
                          Chunks, &Planned) != 0)
    return EFI_COMPROMISED_DATA;
  *ChunkCount = 0;
  for (unsigned I = 0; I < Planned; ++I) {
    EFI_PHYSICAL_ADDRESS At = Chunks[I].base;
    Status = gBS->AllocatePages (AllocateAddress, EfiLoaderData,
                                 EFI_SIZE_TO_PAGES (Chunks[I].size), &At);
    if (EFI_ERROR (Status)) return Status;
    if (At != Chunks[I].base) {
      gBS->FreePages (At, EFI_SIZE_TO_PAGES (Chunks[I].size));
      return EFI_COMPROMISED_DATA;
    }
    *ChunkCount = I + 1;
  }
  Status = SnapshotMap (Map, &MapSize, &Key, &Stride, &Version);
  if (EFI_ERROR (Status) ||
      n986b_validate_arena (Map, MapSize, Stride, CandidateBase,
                            CandidateSize, EfiLoaderData) != 0)
    return EFI_COMPROMISED_DATA;
  *Base = CandidateBase;
  *Size = CandidateSize;
  return EFI_SUCCESS;
}

STATIC VOID FreeArena (
  IN struct n986b_arena_chunk Chunks[N986B_MAX_CHUNKS],
  IN unsigned Count
  )
{
  for (unsigned I = Count; I > 0; --I)
    gBS->FreePages (Chunks[I - 1].base,
                    EFI_SIZE_TO_PAGES (Chunks[I - 1].size));
}

EFI_STATUS EFIAPI UefiMain (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  N986B_RAM_MANIFEST Header;
  struct b2l_macho Mach;
  struct b2l_xnu8796_request LayoutRequest;
  struct b2l_xnu8796_layout Layout;
  struct n986b_dt_config Dt;
  struct b2l_boot_args *Args;
  EFI_LOADED_IMAGE_PROTOCOL *Image = NULL;
  struct n986b_arena_chunk Chunks[N986B_MAX_CHUNKS];
  VOID *Map = NULL;
  unsigned ChunkCount = 0;
  UINTN MapSize, MapKey, Stride, DtSize;
  UINT32 DescriptorVersion;
  UINT64 Base = 0, ArenaSize = 0, KernelBase, KernelSize;
  UINT64 RamdiskPa = 0, DtPa, ArgsPa, EntryPa, Stack;
  UINT64 CpuMpidr, CpuMidr, CurrentEl;
  UINT64 TimerStart, TimerEnd, TimerDelta;
  UINT32 RawCounterFrequency, CounterFrequency, LiveFdtSize;
  UINT8 ManifestDigest[32];
  INTN ParseResult;

  (VOID)SystemTable;
  ZeroMem (&Header, sizeof (Header));
  ZeroMem (&Dt, sizeof (Dt));
  ZeroMem (&Mach, sizeof (Mach));
  ZeroMem (&LayoutRequest, sizeof (LayoutRequest));
  ZeroMem (&Layout, sizeof (Layout));
  ZeroMem (Chunks, sizeof (Chunks));
  ZeroMem (ManifestDigest, sizeof (ManifestDigest));
  for (UINTN Offset = N986B_MARK_MPIDR; Offset <= N986B_MARK_STAGE11_TAG;
       Offset += sizeof (UINT64)) {
    MarkRegister (Offset, 0);
  }
  MarkRegister (N986B_MARK_PARSE, MAX_UINT64);
  MarkRegister (N986B_MARK_STAGE, 1); // Entered loader.
  Print (L"N986B XNU RAM loader preparation (manual, no storage)\r\n");
  CurrentEl = ReadCurrentEl ();
  CpuMpidr = ReadMpidr ();
  CpuMidr = ReadMidr ();
  RawCounterFrequency = ReadCounterFrequency ();
  CounterFrequency = RawCounterFrequency;
  MarkRegister (N986B_MARK_EL, CurrentEl);
  MarkRegister (N986B_MARK_MPIDR, CpuMpidr);
  MarkRegister (N986B_MARK_MIDR, CpuMidr);
  MarkRegister (N986B_MARK_CNTFRQ, RawCounterFrequency);
  Print (L"EL=%lu MPIDR=0x%lx MIDR=0x%lx raw CNTFRQ=%u\r\n",
         CurrentEl, CpuMpidr, CpuMidr, RawCounterFrequency);
  if (CurrentEl != 1 ||
      (CpuMpidr & 0x000000FF00FFFFFFULL) != 0 ||
      ((CpuMidr >> 4) & 0xFFF) != 0xD05 ||
      (RawCounterFrequency != 0 && RawCounterFrequency != N986B_TIMER_HZ)) {
    Print (L"CPU0/26 MHz handoff gate rejected this execution context.\r\n");
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }
  if (RawCounterFrequency == 0) {
    Status = ValidateLiveFdtTimer (&LiveFdtSize);
    if (EFI_ERROR (Status)) {
      Print (L"Live FDT /timer 26 MHz fallback rejected: %r\r\n", Status);
      goto Fail;
    }
    CounterFrequency = N986B_TIMER_HZ;
    Print (L"Validated live FDT /timer: %u Hz, FDT size=%u\r\n",
           CounterFrequency, LiveFdtSize);
  }
  MarkRegister (N986B_MARK_EFFECTIVE_CNTFRQ, CounterFrequency);
  TimerStart = ReadVirtualCounter ();
  for (volatile UINTN Spin = 0; Spin < 100000; ++Spin) {
    __asm__ volatile ("yield" ::: "memory");
  }
  TimerEnd = ReadVirtualCounter ();
  TimerDelta = TimerEnd - TimerStart;
  MarkRegister (N986B_MARK_CNTVCT_START, TimerStart);
  MarkRegister (N986B_MARK_CNTVCT_END, TimerEnd);
  MarkRegister (N986B_MARK_CNTVCT_DELTA, TimerDelta);
  if (TimerDelta != 0) MarkRegister (N986B_MARK_CNTVCT_LIVE, 0x986B0001);
  Print (L"CNTVCT delta=%lu, effective frequency=%u\r\n",
         TimerDelta, CounterFrequency);
  if (RawCounterFrequency == 0 && TimerDelta == 0) {
    Print (L"Zero-CNTFRQ fallback rejected: CNTVCT did not advance.\r\n");
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE, 2); // CPU context accepted.
  Status = VerifyStream (&Header);
  if (EFI_ERROR (Status)) {
    Print (L"RAM manifest or SHA-256 rejected: %r\r\n", Status);
    goto Fail;
  }
  // The USB terminal packet starts at StreamEnd. It must not touch the
  // reserved snapshot page even for a maximal host stream.
  if (Header.StreamEnd + 8 > N986B_SNAPSHOT_BASE) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Fail;
  }
  if (!Sha256HashAll ((CONST VOID *)(UINTN)N986B_MANIFEST_BASE,
                      N986B_MANIFEST_SIZE, ManifestDigest)) {
    Status = EFI_DEVICE_ERROR;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE, 3); // Manifest and SHA-256 accepted.
  ParseResult = b2l_parse_macho ((CONST VOID *)(UINTN)Header.KernelPa,
                                  (UINTN)Header.KernelSize, &Mach);
  MarkRegister (N986B_MARK_PARSE, (UINT64)(INT64)ParseResult);
  if (ParseResult != 0) {
    Print (L"ARM64 Mach-O segments or entry rejected: %d\r\n", ParseResult);
    Status = EFI_LOAD_ERROR;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE, 4); // Mach-O accepted.
  CopyMem (Dt.random_seed, Header.RandomSeed, sizeof (Dt.random_seed));
  Dt.mpidr = CpuMpidr;
  Dt.counter_frequency = CounterFrequency;
  Status = GetVideo (&Dt.video);
  if (EFI_ERROR (Status)) goto Fail;
  MarkRegister (N986B_MARK_STAGE, 5); // GOP accepted.

  Map = AllocatePool (N986B_MAP_SPACE);
  if (!Map) { Status = EFI_OUT_OF_RESOURCES; goto Fail; }
  Status = ReserveArena (Map, Chunks, &ChunkCount, &Base, &ArenaSize);
  if (EFI_ERROR (Status)) {
    Print (L"N986B 1 GiB adjacent-bank arena unavailable: %r\r\n", Status);
    goto Fail;
  }
  /* The planner checks a fresh post-allocation EFI map and the exact Mach-O
   * geometry before any high-RAM write. All following locations come from
   * this single plan, not from the earlier 992 MiB handoff arithmetic. */
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid,
                                (VOID **)&Image);
  if (EFI_ERROR (Status) || Image == NULL ||
      Image->ImageBase == NULL || Image->ImageSize == 0) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  __asm__ volatile ("mov %0, sp" : "=r" (Stack));
  if (Stack < 16384) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  Status = SnapshotMap (Map, &MapSize, &MapKey, &Stride,
                        &DescriptorVersion);
  if (EFI_ERROR (Status)) goto Fail;
  LayoutRequest.kernel_file = (CONST VOID *)(UINTN)Header.KernelPa;
  LayoutRequest.kernel_file_size = (UINTN)Header.KernelSize;
  LayoutRequest.efi_map = Map;
  LayoutRequest.efi_map_bytes = MapSize;
  LayoutRequest.efi_descriptor_stride = Stride;
  LayoutRequest.arena_base = Base;
  LayoutRequest.arena_size = ArenaSize;
  LayoutRequest.ramdisk_size = Header.RamdiskSize;
  LayoutRequest.live.efi_image.base = (UINTN)Image->ImageBase;
  LayoutRequest.live.efi_image.size = Image->ImageSize;
  LayoutRequest.live.efi_stack.base = Stack - 16384;
  LayoutRequest.live.efi_stack.size = 20480;
  LayoutRequest.live.live_fdt.base = N986B_LIVE_FDT_BASE;
  LayoutRequest.live.live_fdt.size = N986B_LIVE_FDT_MAX;
  LayoutRequest.live.usb_stream.base = N986B_MANIFEST_BASE;
  LayoutRequest.live.usb_stream.size = Header.StreamEnd - N986B_MANIFEST_BASE;
  LayoutRequest.live.upload_snapshot.base = N986B_SNAPSHOT_BASE;
  LayoutRequest.live.upload_snapshot.size = N986B_SNAPSHOT_PAGE_SIZE;
  LayoutRequest.live.framebuffer.base = Dt.video.base;
  LayoutRequest.live.framebuffer.size = Dt.video.row_bytes * Dt.video.height;
  if (b2l_xnu8796_plan (&LayoutRequest, &Layout) != B2L_XNU8796_PLAN_OK) {
    Print (L"XNU 8796 memory layout or ownership rejected.\r\n");
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  KernelBase = Layout.physical_base;
  KernelSize = Layout.memory_size;
  MarkRegister (N986B_MARK_LOAD_BASE, KernelBase);
  MarkRegister (N986B_MARK_LOAD_SIZE, KernelSize);
  MarkRegister (N986B_MARK_CONGRUENCE, 0x986B0001);
  MarkRegister (N986B_MARK_STAGE, 6); // Owns the 1 GiB arena.
  MarkRegister (N986B_MARK_ARENA, Base);
  Dt.memory_base = KernelBase;
  Dt.memory_size = KernelSize;
  for (UINTN I = 0; I < Mach.segment_count; ++I) {
    CONST struct b2l_segment *S = &Mach.segments[I];
    UINT64 SegmentPa;
    VOID *Dest;
    if (b2l_xnu8796_segment_physical (&Layout, S, &SegmentPa) !=
        B2L_XNU8796_PLAN_OK) {
      Status = EFI_COMPROMISED_DATA;
      goto Fail;
    }
    Dest = (VOID *)(UINTN)SegmentPa;
    ZeroMem (Dest, (UINTN)S->size);
    CopyMem (Dest, (CONST UINT8 *)(UINTN)Header.KernelPa + S->file_offset,
             (UINTN)S->file_size);
  }
  if (Header.Version == 2) {
    RamdiskPa = Layout.ramdisk_pa;
    CopyMem ((VOID *)(UINTN)RamdiskPa,
             (CONST VOID *)(UINTN)Header.RamdiskPa,
             (UINTN)Header.RamdiskSize);
    Dt.ramdisk_base = RamdiskPa;
    Dt.ramdisk_size = Header.RamdiskSize;
  }
  DtPa = Layout.device_tree_pa;
  Status = n986b_build_device_tree ((VOID *)(UINTN)DtPa,
                                    (UINTN)Layout.device_tree_capacity,
                                    &Dt, &DtSize) == 0 ?
           EFI_SUCCESS : EFI_COMPROMISED_DATA;
  ZeroMem (Dt.random_seed, sizeof (Dt.random_seed));
  ZeroMem (Header.RandomSeed, sizeof (Header.RandomSeed));
  if (EFI_ERROR (Status)) goto Fail;
  MarkRegister (N986B_MARK_STAGE, 7); // Apple DT built.
  MarkRegister (N986B_MARK_DT_PA, DtPa);
  ArgsPa = Layout.boot_args_pa;
  Args = (struct b2l_boot_args *)(UINTN)ArgsPa;
  ZeroMem (Args, sizeof (*Args));
  Args->revision = 2;
  Args->version = 2;
  Args->virtual_base = Layout.virtual_base;
  Args->physical_base = KernelBase;
  Args->memory_size = KernelSize;
  Args->actual_memory_size = KernelSize;
  Args->top_of_kernel_data = Layout.top_of_kernel_data;
  Args->device_tree = Layout.virtual_base + DtPa - KernelBase;
  Args->device_tree_length = (UINT32)DtSize;
  CopyMem (&Args->video, &Dt.video, sizeof (Dt.video));
  Args->video.base |= 1; /* XNU requests a physical framebuffer mapping. */
  AsciiStrCpyS (Args->command_line, sizeof (Args->command_line),
                Header.Version == 2 ?
                "-v debug=0x14e cpus=1 keepsyms=1 rd=md0" :
                "-v debug=0x14e cpus=1 keepsyms=1");
  EntryPa = Layout.entry_pa;
  MarkRegister (N986B_MARK_STAGE, 8); // boot_args and entry calculated.
  MarkRegister (N986B_MARK_ENTRY_PA, EntryPa);
  MarkRegister (N986B_MARK_ARGS_PA, ArgsPa);
  MapSize = N986B_MAP_SPACE;
  Status = SnapshotMap (Map, &MapSize, &MapKey, &Stride, &DescriptorVersion);
  if (EFI_ERROR (Status) ||
      n986b_validate_arena (Map, MapSize, Stride, Base, ArenaSize,
                            EfiLoaderData) != 0) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE, 9); // EFI map confirms arena ownership.
  Print (L"N986B XNU prepared: entry=0x%lx args=0x%lx DT=0x%lx; load=0x%lx size=0x%lx\r\n",
         EntryPa, ArgsPa, DtPa, KernelBase, KernelSize);

  // Keep the branch disabled. Check its exact translation prerequisites and
  // leave a current-manifest-linked receipt in reserved low RAM first.
  MarkRegister (N986B_MARK_STAGE, 10);
  for (UINTN I = 0; I < 4; ++I) {
    UINT64 Word;
    CopyMem (&Word, ManifestDigest + I * sizeof (Word), sizeof (Word));
    MarkRegister (N986B_MARK_MANIFEST_SHA + I * sizeof (Word), Word);
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1101);
  __asm__ volatile ("mov %0, sp" : "=r" (Stack));
  if (Stack < 16384 || Stack > MAX_UINT64 - 4096 ||
      KernelSize > MAX_UINT64 - KernelBase ||
      Args->top_of_kernel_data <= KernelBase ||
      Args->top_of_kernel_data > KernelBase + KernelSize) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1102);
  Status = SnapshotMap (Map, &MapSize, &MapKey, &Stride, &DescriptorVersion);
  if (EFI_ERROR (Status) ||
      n986b_validate_arena (Map, MapSize, Stride, Base, ArenaSize,
                            EfiLoaderData) != 0 ||
      !SnapshotPageIsReserved (Map, MapSize, Stride)) {
    Status = EFI_COMPROMISED_DATA;
    goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1103);
  if (!IdentityRange ((UINTN)Image->ImageBase, Image->ImageSize)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1104);
  if (!IdentityRange (Stack - 16384, 20480)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1105);
  if (!IdentityRange (KernelBase, Args->top_of_kernel_data - KernelBase)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1106);
  if (!IdentityRange (N986B_SNAPSHOT_BASE, N986B_SNAPSHOT_PAGE_SIZE)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1107);
  if (!IdentityWriteRange (N986B_SNAPSHOT_BASE,
                           N986B_SNAPSHOT_PAGE_SIZE)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1108);
  if (!IdentityRange (Dt.video.base,
                      Dt.video.row_bytes * Dt.video.height)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B1109);
  if (!IdentityWriteRange (Dt.video.base,
                           Dt.video.row_bytes * Dt.video.height)) {
    Status = EFI_UNSUPPORTED; goto Fail;
  }
  if (N986B_ENABLE_XNU_HANDOFF == 0) {
    N986B_HANDOFF_SNAPSHOT *Snapshot =
        (N986B_HANDOFF_SNAPSHOT *)(UINTN)N986B_SNAPSHOT_BASE;
    ZeroMem (Snapshot, N986B_SNAPSHOT_PAGE_SIZE);
    CopyMem (Snapshot->Magic, "N986BXHS", sizeof (Snapshot->Magic));
    Snapshot->Version = 3;
    Snapshot->SnapshotSize = sizeof (*Snapshot);
    Snapshot->BootArgsPa = ArgsPa;
    Snapshot->DeviceTreePa = DtPa;
    Snapshot->DeviceTreeSize = (UINT32)DtSize;
    if (!Sha256HashAll ((CONST VOID *)(UINTN)DtPa, DtSize,
                        Snapshot->DeviceTreeSha256)) {
      Status = EFI_DEVICE_ERROR;
      goto Fail;
    }
    CopyMem (Snapshot->BootArgs, Args, sizeof (*Args));
    CopyMem (Snapshot->ManifestSha256, ManifestDigest,
             sizeof (ManifestDigest));
    MarkHandoffSnapshot (Snapshot, N986B_SNAPSHOT_STAGE_PREFLIGHT,
                         EFI_ABORTED);
    MarkRegister (N986B_MARK_STAGE11_TAG, 0x986B0011);
    MarkRegister (N986B_MARK_STAGE, 11);
    Print (L"N986B identity and reserved snapshot preflight passed.\r\n");
    Print (L"Handoff is compile-time gated; returning to UEFI Shell.\r\n");
    Status = EFI_ABORTED;
    goto Fail;
  }
  {
    N986B_HANDOFF_SNAPSHOT *Snapshot;
    struct b2l_handoff_info Handoff;
    __asm__ volatile ("mov %0, sp" : "=r" (Stack));
    if (!IdentityRange ((UINTN)Image->ImageBase, Image->ImageSize) ||
        !IdentityRange (Stack - 16384, 20480) ||
        !IdentityRange (KernelBase, Args->top_of_kernel_data - KernelBase) ||
        !IdentityRange (N986B_SNAPSHOT_BASE, N986B_SNAPSHOT_PAGE_SIZE) ||
        !IdentityWriteRange (N986B_SNAPSHOT_BASE,
                             N986B_SNAPSHOT_PAGE_SIZE) ||
        !IdentityRange (Dt.video.base,
                        Dt.video.row_bytes * Dt.video.height) ||
        !IdentityWriteRange (Dt.video.base,
                             Dt.video.row_bytes * Dt.video.height)) {
      Status = EFI_UNSUPPORTED;
      goto Fail;
    }
    Status = gBS->SetWatchdogTimer (0, 0, 0, NULL);
    if (EFI_ERROR (Status)) goto Fail;
    ZeroMem (&Handoff, sizeof (Handoff));
    Handoff.panel_base = Dt.video.base;
    Handoff.stride = Dt.video.row_bytes;
    Handoff.kernel_start = KernelBase;
    Handoff.kernel_end = Args->top_of_kernel_data;
    Handoff.image_start = (UINTN)Image->ImageBase;
    Handoff.image_end = Handoff.image_start + Image->ImageSize;
    Handoff.stack_start = Stack - 16384;
    Handoff.stack_end = Stack + 4096;
    Status = SnapshotMap (Map, &MapSize, &MapKey, &Stride,
                          &DescriptorVersion);
    if (EFI_ERROR (Status) ||
        !SnapshotPageIsReserved (Map, MapSize, Stride)) {
      Status = EFI_COMPROMISED_DATA;
      goto Fail;
    }
    Snapshot = (N986B_HANDOFF_SNAPSHOT *)(UINTN)N986B_SNAPSHOT_BASE;
    ZeroMem (Snapshot, N986B_SNAPSHOT_PAGE_SIZE);
    CopyMem (Snapshot->Magic, "N986BXHS", sizeof (Snapshot->Magic));
    /* Version 3 records the XNU 8796 736-byte boot_args. Version 2 captured
     * XNU 12377's 1152-byte structure and cannot be decoded as this layout. */
    Snapshot->Version = 3;
    Snapshot->SnapshotSize = sizeof (*Snapshot);
    Snapshot->BootArgsPa = ArgsPa;
    Snapshot->DeviceTreePa = DtPa;
    Snapshot->DeviceTreeSize = (UINT32)DtSize;
    if (!Sha256HashAll ((CONST VOID *)(UINTN)DtPa, DtSize,
                        Snapshot->DeviceTreeSha256)) {
      Status = EFI_DEVICE_ERROR;
      goto Fail;
    }
    CopyMem (Snapshot->BootArgs, Args, sizeof (*Args));
    CopyMem (Snapshot->ManifestSha256, ManifestDigest,
             sizeof (ManifestDigest));
    MarkHandoffSnapshot (Snapshot, N986B_SNAPSHOT_STAGE_IDENTITY,
                         EFI_SUCCESS);
    /* No allocation, console or protocol calls after this point. */
    for (UINTN Attempt = 0; Attempt < 8; ++Attempt) {
      Status = SnapshotMap (Map, &MapSize, &MapKey, &Stride,
                            &DescriptorVersion);
      if (EFI_ERROR (Status) ||
          n986b_validate_arena (Map, MapSize, Stride, Base, ArenaSize,
                                EfiLoaderData) != 0) break;
      Status = gBS->ExitBootServices (ImageHandle, MapKey);
      if (!EFI_ERROR (Status)) {
        MarkHandoffSnapshot (Snapshot, N986B_SNAPSHOT_STAGE_EBS,
                             EFI_SUCCESS);
        MarkHandoffSnapshot (Snapshot, N986B_SNAPSHOT_STAGE_PREBRANCH,
                             EFI_SUCCESS);
        B2LXnuHandoff (EntryPa, ArgsPa, &Handoff);
        CpuDeadLoop ();
      }
      if (Status != EFI_INVALID_PARAMETER) break;
    }
    if (!EFI_ERROR (Status)) Status = EFI_COMPROMISED_DATA;
    MarkHandoffSnapshot (Snapshot, N986B_SNAPSHOT_STAGE_FAILED, Status);
  }
  CpuDeadLoop ();
  return EFI_ABORTED;

Fail:
  MarkRegister (N986B_MARK_STATUS, (UINT64)Status);
  ZeroMem (ManifestDigest, sizeof (ManifestDigest));
  ZeroMem (Dt.random_seed, sizeof (Dt.random_seed));
  ZeroMem (Header.RandomSeed, sizeof (Header.RandomSeed));
  FreeArena (Chunks, ChunkCount);
  if (Map) FreePool (Map);
  MarkRegister (N986B_MARK_RETURN, 0x986B00FF);
  return Status;
}
