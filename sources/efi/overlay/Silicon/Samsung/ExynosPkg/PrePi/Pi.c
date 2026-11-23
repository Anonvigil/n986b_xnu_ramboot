// Pi.c: Entry point for SEC(Security).

#include <PiPei.h>
#include <Pi/PiBootMode.h>

#include <Library/PrePiLib.h>
#include <Library/PrintLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PlatformPrePiLib.h>

#include <Ppi/GuidedSectionExtraction.h>

#include "Pi.h"

#define IS_XIP()  (((UINT64)FixedPcdGet64 (PcdFdBaseAddress) > mSystemMemoryEnd) ||\
                  ((FixedPcdGet64 (PcdFdBaseAddress) + FixedPcdGet32 (PcdFdSize)) <= FixedPcdGet64 (PcdSystemMemoryBase)))

UINT64  mSystemMemoryEnd = FixedPcdGet64 (PcdSystemMemoryBase) +
                           FixedPcdGet64 (PcdSystemMemorySize) - 1;

RETURN_STATUS EFIAPI ArmMmuBaseLibConstructor (VOID);
RETURN_STATUS EFIAPI BaseDebugLibSerialPortConstructor (VOID);
RETURN_STATUS EFIAPI TimerConstructor (VOID);
RETURN_STATUS EFIAPI ExtractGuidedSectionLibConstructor (VOID);
RETURN_STATUS EFIAPI LzmaDecompressLibConstructor (VOID);

STATIC
VOID
SetPrePiCheckpoint (
  IN UINT64 Value
  )
{
  volatile UINT64 *Checkpoint;

  Checkpoint = (volatile UINT64 *)(UINTN)(
                 FixedPcdGet64 (PcdDeviceTreeStore) + sizeof (UINT64));
  *Checkpoint = Value;
  WriteBackDataCacheRange ((VOID *)(UINTN)Checkpoint, sizeof (*Checkpoint));
  ArmDataSynchronizationBarrier ();
}

STATIC
VOID
RunInstrumentedLibraryConstructors (
  VOID
  )
{
  RETURN_STATUS Status;

  SetPrePiCheckpoint (0xB010);
  Status = ArmMmuBaseLibConstructor ();
  SetPrePiCheckpoint (0xB011);
  ASSERT_RETURN_ERROR (Status);

  SetPrePiCheckpoint (0xB020);
  Status = BaseDebugLibSerialPortConstructor ();
  SetPrePiCheckpoint (0xB021);
  ASSERT_RETURN_ERROR (Status);

  SetPrePiCheckpoint (0xB030);
  Status = TimerConstructor ();
  SetPrePiCheckpoint (0xB031);
  ASSERT_RETURN_ERROR (Status);

  SetPrePiCheckpoint (0xB040);
  Status = ExtractGuidedSectionLibConstructor ();
  SetPrePiCheckpoint (0xB041);
  ASSERT_RETURN_ERROR (Status);

  SetPrePiCheckpoint (0xB050);
  Status = LzmaDecompressLibConstructor ();
  SetPrePiCheckpoint (0xB051);
  ASSERT_RETURN_ERROR (Status);
}

VOID
PrePiMain(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{

  EFI_HOB_HANDOFF_INFO_TABLE *HobList;
  EFI_STATUS                  Status;

  UINTN MemoryBase     = 0;
  UINTN MemorySize     = 0;
  UINTN UefiMemoryBase = 0;
  UINTN UefiMemorySize = 0;

  SetPrePiCheckpoint (0x11);

  // Architecture-specific initialization
  // Enable Floating Point
  ArmEnableVFP();

  if (ArmReadCurrentEL() == AARCH64_EL2) {
    // Trap General Exceptions. All exceptions that would be routed to EL1 are routed to EL2
    ArmWriteHcr(ARM_HCR_TGE);

    /* Enable Timer access for non-secure EL1 and EL0
       The cnthctl_el2 register bits are architecturally
       UNKNOWN on reset.
       Disable event stream as it is not in use at this stage
    */
    ArmWriteCntHctl(CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN);
  }

  /* Enable program flow prediction, if supported */
  ArmEnableBranchPrediction();

  // Declare UEFI region
  MemoryBase     = FixedPcdGet32(PcdSystemMemoryBase);
  MemorySize     = FixedPcdGet32(PcdSystemMemorySize);
  UefiMemoryBase = FixedPcdGet32(PcdUefiMemPoolBase);
  UefiMemorySize = FixedPcdGet32(PcdUefiMemPoolSize);
  StackBase      = (VOID *)(UefiMemoryBase + UefiMemorySize - StackSize);

  DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx \n"
       "Stack Base = 0x%llx, Stack Size = 0x%llx \n",
       UefiMemoryBase, UefiMemorySize, StackBase, StackSize));

  // Set up HOB
  HobList = HobConstructor(
      (VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase,
      StackBase);

  PrePeiSetHobList (HobList);
  SetPrePiCheckpoint (0x12);

  // Invalidate cache
  InvalidateDataCacheRange(
      (VOID *)(UINTN)PcdGet64(PcdFdBaseAddress), PcdGet32(PcdFdSize));
  SetPrePiCheckpoint (0x13);

  // Initialize MMU
  SetPrePiCheckpoint (0x14);
  Status = MemoryPeim(UefiMemoryBase, UefiMemorySize);
  ASSERT_EFI_ERROR (Status);
  SetPrePiCheckpoint (0x15);

  // Framebuffer and MMIO initialization must run only after the platform
  // memory attributes and translation tables are active.  Running the
  // 17.8 MiB framebuffer clear with M=0 made any abort indistinguishable
  // from a transport failure and performed millions of uncached stores.
  PlatformInitialize();
  SetPrePiCheckpoint (0x16);

  // Add HOBs
  BuildStackHob ((UINTN)StackBase, StackSize);

  // TODO: Call CpuPei as a library
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs (CpuHob and FvHob)
  DEBUG((EFI_D_INFO, "PlatformPeim In \n"));
  Status = PlatformPeim();
  ASSERT_EFI_ERROR (Status);
  SetPrePiCheckpoint (0xB001);

  // Now, the HOB List has been initialized, we can register performance information
  // PERF_START (NULL, "PEI", NULL, StartTimeStamp);

  // SEC phase needs to run library constructors by hand.
  RunInstrumentedLibraryConstructors ();
  SetPrePiCheckpoint (0x171);

  // Assume the FV that contains the SEC (our code) also contains a compressed FV.
  DEBUG((EFI_D_INFO, "DecompressFirstFv In \n"));
  SetPrePiCheckpoint (0x172);
  Status = DecompressFirstFv();
  SetPrePiCheckpoint (0x173);
  ASSERT_EFI_ERROR (Status);
  SetPrePiCheckpoint (0xC01A);

  // Load the DXE Core and transfer control to it
  DEBUG((EFI_D_INFO, "LoadDxeCoreFromFv In \n"));
  SetPrePiCheckpoint (0x181);
  Status = LoadDxeCoreFromFv(NULL, 0);
  SetPrePiCheckpoint (0x188);
  ASSERT_EFI_ERROR (Status);
}

VOID
CEntryPoint(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{
  SetPrePiCheckpoint (0x10);

  // Goto primary Main.
  PrePiMain(StackBase, StackSize);

  // DXE Core should always load and never return
  ASSERT(FALSE);
}

VOID
SecondaryCEntryPoint(
  IN  UINTN  MpId
  )
{
  // We must never get into this function on UniCore system
  ASSERT(FALSE);
}
