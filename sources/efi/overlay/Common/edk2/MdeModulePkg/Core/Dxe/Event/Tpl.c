/** @file
  Task priority (TPL) functions.

Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "DxeMain.h"
#include "Event.h"

#if defined (N986B_DXE_PROBE)
STATIC UINTN  mN986BRestoreTplDepth;
STATIC UINTN  mN986BRestoreTplSequence;

STATIC
VOID
TraceN986BTplValue (
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

#define TraceN986BRestoreTpl(Stage)       TraceN986BTplValue (0x280, (Stage))
#define TraceN986BInterruptState(Stage)  TraceN986BTplValue (0x2A8, (Stage))
#else
#define TraceN986BTplValue(Offset, Value)
#define TraceN986BRestoreTpl(Stage)
#define TraceN986BInterruptState(Stage)
#endif

/**
  Set Interrupt State.

  @param  Enable  The state of enable or disable interrupt

**/
VOID
CoreSetInterruptState (
  IN BOOLEAN  Enable
  )
{
  EFI_STATUS  Status;
  BOOLEAN     InSmm;

  TraceN986BTplValue (0x2B0, (UINTN)gCpu);
  TraceN986BInterruptState (0xF420);
  if (gCpu == NULL) {
    TraceN986BInterruptState (0xF421);
    return;
  }

  if (!Enable) {
    TraceN986BInterruptState (0xF422);
    gCpu->DisableInterrupt (gCpu);
    TraceN986BInterruptState (0xF423);
    return;
  }

  if (gSmmBase2 == NULL) {
    TraceN986BInterruptState (0xF424);
    gCpu->EnableInterrupt (gCpu);
    TraceN986BInterruptState (0xF425);
    return;
  }

  Status = gSmmBase2->InSmm (gSmmBase2, &InSmm);
  if (!EFI_ERROR (Status) && !InSmm) {
    TraceN986BInterruptState (0xF424);
    gCpu->EnableInterrupt (gCpu);
    TraceN986BInterruptState (0xF425);
  }
}

/**
  Raise the task priority level to the new level.
  High level is implemented by disabling processor interrupts.

  @param  NewTpl  New task priority level

  @return The previous task priority level

**/
EFI_TPL
EFIAPI
CoreRaiseTpl (
  IN EFI_TPL  NewTpl
  )
{
  EFI_TPL  OldTpl;

  OldTpl = gEfiCurrentTpl;
  if (OldTpl > NewTpl) {
    DEBUG ((DEBUG_ERROR, "FATAL ERROR - RaiseTpl with OldTpl(0x%x) > NewTpl(0x%x)\n", OldTpl, NewTpl));
    ASSERT (FALSE);
  }

  ASSERT (VALID_TPL (NewTpl));

  //
  // If raising to high level, disable interrupts
  //
  if ((NewTpl >= TPL_HIGH_LEVEL) &&  (OldTpl < TPL_HIGH_LEVEL)) {
    CoreSetInterruptState (FALSE);
  }

  //
  // Set the new value
  //
  gEfiCurrentTpl = NewTpl;

  return OldTpl;
}

/**
  Lowers the task priority to the previous value.   If the new
  priority unmasks events at a higher priority, they are dispatched.

  @param  NewTpl  New, lower, task priority

**/
VOID
EFIAPI
CoreRestoreTpl (
  IN EFI_TPL  NewTpl
  )
{
  EFI_TPL  OldTpl;
  EFI_TPL  PendingTpl;

#if defined (N986B_DXE_PROBE)
  mN986BRestoreTplDepth++;
  mN986BRestoreTplSequence++;
  TraceN986BTplValue (0x2B8, mN986BRestoreTplDepth);
  TraceN986BTplValue (0x2E0, mN986BRestoreTplSequence);
#endif
  OldTpl = gEfiCurrentTpl;
  TraceN986BTplValue (0x288, OldTpl);
  TraceN986BTplValue (0x290, NewTpl);
  TraceN986BTplValue (0x298, gEventPending);
  TraceN986BRestoreTpl (0xF410);
  if (NewTpl > OldTpl) {
    DEBUG ((DEBUG_ERROR, "FATAL ERROR - RestoreTpl with NewTpl(0x%x) > OldTpl(0x%x)\n", NewTpl, OldTpl));
    ASSERT (FALSE);
  }

  ASSERT (VALID_TPL (NewTpl));

  //
  // If lowering below HIGH_LEVEL, make sure
  // interrupts are enabled
  //

  if ((OldTpl >= TPL_HIGH_LEVEL) &&  (NewTpl < TPL_HIGH_LEVEL)) {
    gEfiCurrentTpl = TPL_HIGH_LEVEL;
  }
  TraceN986BRestoreTpl (0xF411);

  //
  // Dispatch any pending events
  //
  while (gEventPending != 0) {
    PendingTpl = (UINTN)HighBitSet64 (gEventPending);
    TraceN986BTplValue (0x298, gEventPending);
    TraceN986BTplValue (0x2A0, PendingTpl);
    TraceN986BRestoreTpl (0xF412);
    if (PendingTpl <= NewTpl) {
      break;
    }

    gEfiCurrentTpl = PendingTpl;
    if (gEfiCurrentTpl < TPL_HIGH_LEVEL) {
      TraceN986BRestoreTpl (0xF413);
      CoreSetInterruptState (TRUE);
      TraceN986BRestoreTpl (0xF414);
    }

    TraceN986BRestoreTpl (0xF415);
    CoreDispatchEventNotifies (gEfiCurrentTpl);
    TraceN986BRestoreTpl (0xF416);
  }
  TraceN986BRestoreTpl (0xF417);

  //
  // Set the new value
  //

  gEfiCurrentTpl = NewTpl;

  //
  // If lowering below HIGH_LEVEL, make sure
  // interrupts are enabled
  //
  if (gEfiCurrentTpl < TPL_HIGH_LEVEL) {
    TraceN986BRestoreTpl (0xF418);
    CoreSetInterruptState (TRUE);
  }
#if defined (N986B_DXE_PROBE)
  mN986BRestoreTplDepth--;
  TraceN986BTplValue (0x2B8, mN986BRestoreTplDepth);
#endif
  TraceN986BRestoreTpl (0xF419);
}
