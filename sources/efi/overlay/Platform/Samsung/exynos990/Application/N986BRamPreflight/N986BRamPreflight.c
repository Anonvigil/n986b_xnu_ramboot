/** @file
  Verify the transient N986B XNU stream before attempting a kernel loader.

  The USB callback deposits this image entirely in RAM. This application does
  not access partitions, leave UEFI boot services, or branch to the Mach-O.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define N986B_MANIFEST_BASE  0x90700000ULL
#define N986B_KERNEL_BASE    0x90701000ULL
#define N986B_STREAM_LIMIT   0x95D00000ULL
#define N986B_MANIFEST_SIZE  0x1000U
#define N986B_STREAM_ALIGN   0x10000ULL
#define N986B_KERNEL_MAX     (128ULL * 1024ULL * 1024ULL)
#define N986B_MARKER_BASE    0x95E00000ULL

// 8-byte, cache-cleaned Upload readback fields at +0x800 through +0x830.
#define MARK_ENTRY       0x800U
#define MARK_MANIFEST    0x808U
#define MARK_MACHO       0x810U
#define MARK_SHA256      0x818U
#define MARK_RETURN      0x820U
#define MARK_ERROR       0x828U
#define MARK_EFI_STATUS  0x830U

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
  UINT8   Reserved[N986B_MANIFEST_SIZE - 332];
} N986B_RAM_MANIFEST;
#pragma pack (pop)

STATIC_ASSERT (sizeof (N986B_RAM_MANIFEST) == N986B_MANIFEST_SIZE, "N986B manifest size");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, Version) == 8, "N986B version offset");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, KernelPa) == 16, "N986B kernel address offset");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, KernelSha256) == 40, "N986B digest offset");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, RandomSeed) == 72, "N986B seed offset");
STATIC_ASSERT (OFFSET_OF (N986B_RAM_MANIFEST, Flags) == 328, "N986B flags offset");

STATIC CONST UINT8 mManifestMagic[8] = { 'N', '9', '8', '6', 'B', 'X', 'N', 'U' };

STATIC
VOID
Mark (
  IN UINTN   Offset,
  IN UINT64  Value
  )
{
  volatile UINT64  *Slot;

  Slot  = (volatile UINT64 *)(UINTN)(N986B_MARKER_BASE + Offset);
  *Slot = Value;
  WriteBackDataCacheRange ((VOID *)(UINTN)Slot, sizeof (*Slot));
  MemoryFence ();
}

STATIC
EFI_STATUS
Reject (
  IN EFI_STATUS  Status,
  IN CHAR16      *Reason,
  IN UINT64      ErrorCode
  )
{
  Mark (MARK_ERROR, ErrorCode);
  Mark (MARK_EFI_STATUS, (UINT64)Status);
  Print (L"N986B XNU RAM preflight: %s (%r)\r\n", Reason, Status);
  Print (L"Returning to UEFI Shell; no kernel was started.\r\n");
  gBS->Stall (1500000);
  Mark (MARK_RETURN, 0x986B00FF);
  return Status;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  N986B_RAM_MANIFEST  Header;
  UINT8               Digest[32];
  CONST UINT8         *Kernel;
  UINTN               Index;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  for (Index = MARK_ENTRY; Index <= MARK_EFI_STATUS; Index += sizeof (UINT64)) {
    Mark (Index, 0);
  }
  Mark (MARK_ENTRY, 0x986B0001);

  // The USB stream was written while the previous bootloader's D-cache was
  // disabled. Drop any stale EL1 cache lines before reading the reserved RAM.
  InvalidateDataCacheRange ((VOID *)(UINTN)N986B_MANIFEST_BASE, N986B_MANIFEST_SIZE);
  CopyMem (&Header, (CONST VOID *)(UINTN)N986B_MANIFEST_BASE, sizeof (Header));

  if (CompareMem (Header.Magic, mManifestMagic, sizeof (mManifestMagic)) != 0) {
    return Reject (EFI_NOT_FOUND, L"manifest absent or magic mismatch", 1);
  }
  if (Header.Version != 1 || Header.HeaderSize != N986B_MANIFEST_SIZE ||
      Header.KernelPa != N986B_KERNEL_BASE || Header.Flags != 0) {
    return Reject (EFI_COMPROMISED_DATA, L"manifest version, address, or flags invalid", 2);
  }
  if (Header.StreamEnd <= N986B_KERNEL_BASE ||
      Header.StreamEnd > N986B_STREAM_LIMIT ||
      (Header.StreamEnd & (N986B_STREAM_ALIGN - 1)) != 0 ||
      Header.KernelSize < 4096 || Header.KernelSize > N986B_KERNEL_MAX ||
      Header.KernelSize > Header.StreamEnd - N986B_KERNEL_BASE) {
    return Reject (EFI_BAD_BUFFER_SIZE, L"kernel length or stream bounds invalid", 3);
  }
  for (Index = 0; Index < sizeof (Header.Reserved); ++Index) {
    if (Header.Reserved[Index] != 0) {
      return Reject (EFI_COMPROMISED_DATA, L"manifest reserved bytes are nonzero", 4);
    }
  }
  Mark (MARK_MANIFEST, 0x986B0002);

  Kernel = (CONST UINT8 *)(UINTN)Header.KernelPa;
  InvalidateDataCacheRange (
    (VOID *)(UINTN)Header.KernelPa,
    (UINTN)(Header.StreamEnd - Header.KernelPa)
    );
  if (*(CONST UINT32 *)Kernel != 0xFEEDFACF ||
      *(CONST UINT32 *)(Kernel + 4) != 0x0100000C) {
    return Reject (EFI_LOAD_ERROR, L"payload is not an ARM64 Mach-O", 5);
  }
  Mark (MARK_MACHO, 0x986B0003);
  if (!Sha256HashAll (Kernel, (UINTN)Header.KernelSize, Digest)) {
    return Reject (EFI_DEVICE_ERROR, L"SHA-256 calculation failed", 6);
  }
  if (CompareMem (Digest, Header.KernelSha256, sizeof (Digest)) != 0) {
    ZeroMem (Digest, sizeof (Digest));
    return Reject (EFI_COMPROMISED_DATA, L"Mach-O SHA-256 mismatch", 7);
  }
  ZeroMem (Digest, sizeof (Digest));
  Mark (MARK_SHA256, 0x986B0004);
  for (Index = (UINTN)Header.KernelSize;
       Index < (UINTN)(Header.StreamEnd - Header.KernelPa);
       ++Index) {
    if (Kernel[Index] != 0) {
      return Reject (EFI_COMPROMISED_DATA, L"stream padding is nonzero", 8);
    }
  }
  Mark (MARK_ERROR, 0);
  Mark (MARK_EFI_STATUS, (UINT64)EFI_ABORTED);
  Print (L"N986B XNU RAM preflight: PASS, Mach-O %lu bytes at 0x%lx\r\n",
         Header.KernelSize, Header.KernelPa);
  Print (L"No XNU handoff in this diagnostic build; returning to UEFI Shell.\r\n");
  gBS->Stall (1500000);
  Mark (MARK_RETURN, 0x986B00FF);
  return EFI_ABORTED;
}
