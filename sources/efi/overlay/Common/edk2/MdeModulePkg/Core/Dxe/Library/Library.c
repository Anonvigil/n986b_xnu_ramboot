/** @file
  DXE Core library services.

Copyright (c) 2006 - 2008, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "DxeMain.h"
#if defined (N986B_DXE_PROBE)
#include "../Hand/Handle.h"

STATIC UINTN  mN986BProtocolLockReleaseDepth;

STATIC
VOID
TraceN986BProtocolLockValue (
  IN UINTN   Offset,
  IN UINT64  Value
  )
{
  volatile UINT64  *Slot;

  Slot  = (volatile UINT64 *)(UINTN)(0x95E00000ULL + Offset);
  *Slot = Value;
  WriteBackDataCacheRange ((VOID *)(UINTN)Slot, sizeof (*Slot));
  MemoryFence ();
}

#define TraceN986BProtocolLockStage(Stage)  TraceN986BProtocolLockValue (0x248, (Stage))
#endif

//
// Lock Stuff
//

/**
  Initialize a basic mutual exclusion lock.   Each lock
  provides mutual exclusion access at it's task priority
  level.  Since there is no-premption (at any TPL) or
  multiprocessor support, acquiring the lock only consists
  of raising to the locks TPL.

  @param  Lock               The EFI_LOCK structure to initialize

  @retval EFI_SUCCESS        Lock Owned.
  @retval EFI_ACCESS_DENIED  Reentrant Lock Acquisition, Lock not Owned.

**/
EFI_STATUS
CoreAcquireLockOrFail (
  IN EFI_LOCK  *Lock
  )
{
  ASSERT (Lock != NULL);
  ASSERT (Lock->Lock != EfiLockUninitialized);

  if (Lock->Lock == EfiLockAcquired) {
    //
    // Lock is already owned, so bail out
    //
    return EFI_ACCESS_DENIED;
  }

  Lock->OwnerTpl = CoreRaiseTpl (Lock->Tpl);

  Lock->Lock = EfiLockAcquired;
  return EFI_SUCCESS;
}

/**
  Raising to the task priority level of the mutual exclusion
  lock, and then acquires ownership of the lock.

  @param  Lock               The lock to acquire

  @return Lock owned

**/
VOID
CoreAcquireLock (
  IN EFI_LOCK  *Lock
  )
{
  ASSERT (Lock != NULL);
  ASSERT (Lock->Lock == EfiLockReleased);

  Lock->OwnerTpl = CoreRaiseTpl (Lock->Tpl);
  Lock->Lock     = EfiLockAcquired;
}

/**
  Releases ownership of the mutual exclusion lock, and
  restores the previous task priority level.

  @param  Lock               The lock to release

  @return Lock unowned

**/
VOID
CoreReleaseLock (
  IN EFI_LOCK  *Lock
  )
{
  EFI_TPL  Tpl;
#if defined (N986B_DXE_PROBE)
  BOOLEAN  ProbeProtocolLock;

  ProbeProtocolLock = (Lock == &gProtocolDatabaseLock);
  if (ProbeProtocolLock) {
    mN986BProtocolLockReleaseDepth++;
    TraceN986BProtocolLockValue (0x268, mN986BProtocolLockReleaseDepth);
    TraceN986BProtocolLockValue (0x258, gEfiCurrentTpl);
    TraceN986BProtocolLockValue (0x260, Lock->Lock);
    TraceN986BProtocolLockStage (0xF400);
  }
#endif

  ASSERT (Lock != NULL);
  ASSERT (Lock->Lock == EfiLockAcquired);

  Tpl = Lock->OwnerTpl;
#if defined (N986B_DXE_PROBE)
  if (ProbeProtocolLock) {
    TraceN986BProtocolLockValue (0x250, Tpl);
    TraceN986BProtocolLockStage (0xF401);
  }
#endif

  Lock->Lock = EfiLockReleased;
#if defined (N986B_DXE_PROBE)
  if (ProbeProtocolLock) {
    TraceN986BProtocolLockValue (0x260, Lock->Lock);
    TraceN986BProtocolLockStage (0xF402);
    TraceN986BProtocolLockStage (0xF403);
  }
#endif

  CoreRestoreTpl (Tpl);
#if defined (N986B_DXE_PROBE)
  if (ProbeProtocolLock) {
    TraceN986BProtocolLockValue (0x258, gEfiCurrentTpl);
    mN986BProtocolLockReleaseDepth--;
    TraceN986BProtocolLockValue (0x268, mN986BProtocolLockReleaseDepth);
    TraceN986BProtocolLockStage (0xF404);
  }
#endif
}
