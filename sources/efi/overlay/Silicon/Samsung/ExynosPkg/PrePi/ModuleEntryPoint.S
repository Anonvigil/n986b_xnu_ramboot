#include <Library/PcdLib.h>
#include <AsmMacroIoLibV8.h>
#include <Chipset/AArch64.h>

#define LoadConstantToReg(Data, Reg) \
  ldr  Reg, =Data

.text
.align 3

GCC_ASM_IMPORT (CEntryPoint)
GCC_ASM_IMPORT (ArmWriteCpacr)
GCC_ASM_IMPORT (ArmEnableInstructionCache)
GCC_ASM_IMPORT (ArmEnableDataCache)
GCC_ASM_IMPORT (ArmInvalidateTlb)
GCC_ASM_IMPORT (ArmDeInitialize)

GCC_ASM_EXPORT (_ModuleEntryPoint)

.global _StackBase 
.global _StackSize

_StackBase:
  .quad FixedPcdGet32(PcdPrePiStackBase)

_StackSize:
  .quad FixedPcdGet32(PcdPrePiStackSize)


_ModuleEntryPoint:
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  str x0, [x10] // oh my fdt
  mov x11, #1
  str x11, [x10, #8] // direct handoff entered
  mrs x11, VBAR_EL1
  str x11, [x10, #16] // resident S-Boot vector retained for diagnostics
  mrs x11, SCTLR_EL1
  str x11, [x10, #24]
  mrs x11, CurrentEL
  str x11, [x10, #32]
  dsb sy
  
  mov x0, #0

  /* Know about the current CPU index */
  mrs x3, mpidr_el1
  mov x2, x3
  mov x1, #0x0000100
  and x2, x2, x1
  cmp x2, #0x0
  mov x0, #0
  beq _CpuIndex

  mov x0, #4
_CpuIndex:
  mov x2, x3
  mov x1, #0xf
  and x2, x2, x1
  add x0, x0, x2

  /* Keep the CPU index in x19 */
  mov x19, x0


  /* First ensure all interrupts are disabled */
  bl ASM_PFX(ArmDisableInterrupts)

  /* Ensure that the MMU and caches are off */
  bl ASM_PFX(ArmDisableCachesAndMmu)
 
  /* Invalide I-Cache */
  bl ASM_PFX(ArmInvalidateInstructionCache)
  
  /* Invalidate TLB */
  bl ASM_PFX(ArmInvalidateTlb)

_SetupExceptionVector:
  /* Keep the resident S-Boot VBAR until DXE installs a real exception
     handler.  The former table contained only `b .`, which converted every
     early synchronous exception into a silent permanent hang. */
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  mov x11, #2
  str x11, [x10, #8] // cache/MMU shutdown and TLB invalidation complete
  dsb sy
    
_DonNotTrap_VFP_SIMD:
  mrs x0, CPACR_EL1
  /* Bit 20 and 21 */
  orr x0, x0, #0x300000
  msr CPACR_EL1, x0
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  mov x11, #3
  str x11, [x10, #8] // FP/SIMD access configured
  dsb sy
    
_SetupPrimaryCoreStack:
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */
  
  add x4, x0, x1
  add sp, x4, #0
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  mov x11, #4
  str x11, [x10, #8] // primary stack installed
  dsb sy

_EnableCache: 
  bl ArmInvalidateDataCache
  bl ASM_PFX(ArmEnableInstructionCache)
  bl ASM_PFX(ArmEnableDataCache)
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  mov x11, #5
  str x11, [x10, #8] // cache enable sequence returned
  dsb sy

_PrepareArguments:
  /* x0 = _StackBase and x1 = _StackSize */
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */

  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  mov x11, #6
  str x11, [x10, #8] // entering CEntryPoint
  dsb sy

  bl CEntryPoint

.align 3
dead:  
  b dead                      /* We should never get here */
