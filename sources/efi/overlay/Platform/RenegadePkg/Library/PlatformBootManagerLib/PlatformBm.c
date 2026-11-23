/** @file
  Implementation for PlatformBootManagerLib library class interfaces.

  Copyright (C) 2015-2016, Red Hat, Inc.
  Copyright (c) 2014 - 2019, ARM Ltd. All rights reserved.<BR>
  Copyright (c) 2004 - 2018, Intel Corporation. All rights reserved.<BR>
  Copyright (c) 2016, Linaro Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Guid/EventGroup.h>
#include <Guid/SerialPortLibVendor.h>
#include <Guid/TtyTerm.h>
#include <IndustryStandard/Pci22.h>
#include <Library/BootLogoLib.h>
#if defined (N986B_DXE_PROBE)
#include <Library/CacheMaintenanceLib.h>
#endif
#include <Library/CapsuleLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Library/MsPlatformDevicesLib.h>

#include <Protocol/DevicePath.h>
#include <Protocol/EsrtManagement.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Protocol/PlatformBootManager.h>

#include "PlatformBm.h"

#if defined (N986B_DXE_PROBE)
#if defined (N986B_RECOVERY_BOOT)
// Recovery diagnostic and branch FDs have separate firmware-file GUIDs.
STATIC CONST EFI_GUID mN986BRecoveryFileGuid = {
#if defined (N986B_RECOVERY_UI_HIGH_BOOT)
  0x40C945F7, 0x43B9, 0x41A5, { 0x99, 0x50, 0x49, 0x9E, 0x2F, 0xBC, 0x07, 0x27 }
#elif defined (N986B_RECOVERY_UI_HIGH_DUAL_BOOT)
  0x1AC3C292, 0x5073, 0x41B4, { 0xBA, 0x23, 0xB5, 0xD3, 0xC8, 0x20, 0xA5, 0xFC }
#elif defined (N986B_RECOVERY_UI_HIGH_DUAL_ECT_BOOT)
  0x2F9380D4, 0x6C74, 0x44E8, { 0x9A, 0x7B, 0x43, 0x96, 0xF5, 0xAE, 0xB2, 0xE1 }
#elif defined (N986B_RECOVERY_UI_BOOT)
  0xF495AF3B, 0x8B57, 0x4C1E, { 0x93, 0x4C, 0x52, 0x0D, 0x64, 0x5B, 0x81, 0x48 }
#elif defined (N986B_RECOVERY_RELOCATED_ECT_HOLD_BOOT)
  0xA343ED0B, 0x8AFF, 0x4B1B, { 0xAE, 0x72, 0x6D, 0xA0, 0xBD, 0x12, 0x84, 0x85 }
#elif defined (N986B_RECOVERY_RELOCATED_ECT_BRANCH_BOOT)
  0xF9F65EBA, 0x151E, 0x4DFC, { 0xAA, 0x46, 0xD6, 0x01, 0x5D, 0x92, 0x4D, 0xD9 }
#elif defined (N986B_RECOVERY_RELOCATED_TRIPLE_BLACKLIST_BOOT)
  0x8DF89D77, 0x699C, 0x4776, { 0xAC, 0x46, 0x15, 0xDE, 0x83, 0x92, 0x46, 0xE6 }
#elif defined (N986B_RECOVERY_RELOCATED_DUAL_BLACKLIST_BOOT)
  0xE1A72CB7, 0x1376, 0x4B97, { 0xA4, 0xD5, 0x8C, 0xF9, 0xC1, 0xD6, 0x9D, 0x29 }
#elif defined (N986B_RECOVERY_RELOCATED_BRANCH_BOOT)
  0x4642925B, 0xCB47, 0x4094, { 0x8A, 0x3B, 0x40, 0xA7, 0xA7, 0x6B, 0x8F, 0x36 }
#elif defined (N986B_RECOVERY_RELOCATED_HOLD_BOOT)
  0x2F69404D, 0x9BDD, 0x41DE, { 0xAC, 0x7C, 0x60, 0x6B, 0xC9, 0x3C, 0x8D, 0xB6 }
#elif defined (N986B_RECOVERY_HOLD_BOOT)
  0xCA095856, 0x2D6D, 0x42AB, { 0x93, 0xE4, 0x3C, 0x4A, 0x30, 0xF6, 0x96, 0x1C }
#elif defined (N986B_RECOVERY_BRANCH_BOOT)
  0xA1630BC5, 0x0D93, 0x4D8F, { 0x8E, 0x37, 0x0E, 0x36, 0xE0, 0xA7, 0x8B, 0x32 }
#else
  0x6DDAEEB4, 0x98E3, 0x455F, { 0x9B, 0x14, 0x81, 0x54, 0xEA, 0x1E, 0x63, 0xB7 }
#endif
};
#else
// Must match N986BXnuLoader.inf FILE_GUID. This option is RAM-only.
STATIC CONST EFI_GUID mN986BXnuLoaderFileGuid = {
  0x6993C426, 0xC19A, 0x4C89, { 0x8B, 0x90, 0xEE, 0x39, 0xA6, 0xE7, 0xF8, 0xA1 }
};
#endif
#endif

#if defined (N986B_DXE_PROBE)
STATIC
VOID
TraceN986BPlatformBmValue (
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
#else
#define TraceN986BPlatformBmValue(Offset, Value)
#endif

#define DP_NODE_LEN(Type)                                                      \
  {                                                                            \
    (UINT8)sizeof(Type), (UINT8)(sizeof(Type) >> 8)                            \
  }

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH         SerialDxe;
  UART_DEVICE_PATH           Uart;
  VENDOR_DEFINED_DEVICE_PATH TermType;
  EFI_DEVICE_PATH_PROTOCOL   End;
} PLATFORM_SERIAL_CONSOLE;
#pragma pack()

STATIC PLATFORM_SERIAL_CONSOLE mSerialConsole = {
    //
    // VENDOR_DEVICE_PATH SerialDxe
    //
    {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, DP_NODE_LEN(VENDOR_DEVICE_PATH)},
     EDKII_SERIAL_PORT_LIB_VENDOR_GUID},

    //
    // UART_DEVICE_PATH Uart
    //
    {
        {MESSAGING_DEVICE_PATH, MSG_UART_DP, DP_NODE_LEN(UART_DEVICE_PATH)},
        0,                                     // Reserved
        FixedPcdGet64(PcdUartDefaultBaudRate), // BaudRate
        FixedPcdGet8(PcdUartDefaultDataBits),  // DataBits
        FixedPcdGet8(PcdUartDefaultParity),    // Parity
        FixedPcdGet8(PcdUartDefaultStopBits)   // StopBits
    },

    //
    // VENDOR_DEFINED_DEVICE_PATH TermType
    //
    {
        {MESSAGING_DEVICE_PATH, MSG_VENDOR_DP,
         DP_NODE_LEN(VENDOR_DEFINED_DEVICE_PATH)}
        //
        // Guid to be filled in dynamically
        //
    },

    //
    // EFI_DEVICE_PATH_PROTOCOL End
    //
    {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
     DP_NODE_LEN(EFI_DEVICE_PATH_PROTOCOL)}};

#pragma pack(1)
typedef struct {
  USB_CLASS_DEVICE_PATH    Keyboard;
  EFI_DEVICE_PATH_PROTOCOL End;
} PLATFORM_USB_KEYBOARD;
#pragma pack()

STATIC PLATFORM_USB_KEYBOARD mUsbKeyboard = {
    //
    // USB_CLASS_DEVICE_PATH Keyboard
    //
    {
        {MESSAGING_DEVICE_PATH, MSG_USB_CLASS_DP,
         DP_NODE_LEN(USB_CLASS_DEVICE_PATH)},
        0xFFFF, // VendorId: any
        0xFFFF, // ProductId: any
        3,      // DeviceClass: HID
        1,      // DeviceSubClass: boot
        1       // DeviceProtocol: keyboard
    },

    //
    // EFI_DEVICE_PATH_PROTOCOL End
    //
    {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
     DP_NODE_LEN(EFI_DEVICE_PATH_PROTOCOL)}};

/**
  Check if the handle satisfies a particular condition.

  @param[in] Handle      The handle to check.
  @param[in] ReportText  A caller-allocated string passed in for reporting
                         purposes. It must never be NULL.

  @retval TRUE   The condition is satisfied.
  @retval FALSE  Otherwise. This includes the case when the condition could not
                 be fully evaluated due to an error.
**/
typedef BOOLEAN(EFIAPI *FILTER_FUNCTION)(
    IN EFI_HANDLE Handle, IN CONST CHAR16 *ReportText);

/**
  Process a handle.

  @param[in] Handle      The handle to process.
  @param[in] ReportText  A caller-allocated string passed in for reporting
                         purposes. It must never be NULL.
**/
typedef VOID(EFIAPI *CALLBACK_FUNCTION)(
    IN EFI_HANDLE Handle, IN CONST CHAR16 *ReportText);

/**
  Locate all handles that carry the specified protocol, filter them with a
  callback function, and pass each handle that passes the filter to another
  callback.

  @param[in] ProtocolGuid  The protocol to look for.

  @param[in] Filter        The filter function to pass each handle to. If this
                           parameter is NULL, then all handles are processed.

  @param[in] Process       The callback function to pass each handle to that
                           clears the filter.
**/
STATIC
VOID FilterAndProcess(
    IN EFI_GUID *ProtocolGuid, IN FILTER_FUNCTION Filter OPTIONAL,
    IN CALLBACK_FUNCTION Process)
{
  EFI_STATUS  Status;
  EFI_HANDLE *Handles;
  UINTN       NoHandles;
  UINTN       Idx;

  Status = gBS->LocateHandleBuffer(
      ByProtocol, ProtocolGuid, NULL /* SearchKey */, &NoHandles, &Handles);
  if (EFI_ERROR(Status)) {
    //
    // This is not an error, just an informative condition.
    //
    DEBUG((EFI_D_VERBOSE, "%a: %g: %r\n", __FUNCTION__, ProtocolGuid, Status));
    return;
  }

  ASSERT(NoHandles > 0);
  for (Idx = 0; Idx < NoHandles; ++Idx) {
    CHAR16 *      DevicePathText;
    STATIC CHAR16 Fallback[] = L"<device path unavailable>";

    //
    // The ConvertDevicePathToText() function handles NULL input transparently.
    //
    DevicePathText = ConvertDevicePathToText(
        DevicePathFromHandle(Handles[Idx]),
        FALSE, // DisplayOnly
        FALSE  // AllowShortcuts
    );
    if (DevicePathText == NULL) {
      DevicePathText = Fallback;
    }

    if (Filter == NULL || Filter(Handles[Idx], DevicePathText)) {
      Process(Handles[Idx], DevicePathText);
    }

    if (DevicePathText != Fallback) {
      FreePool(DevicePathText);
    }
  }
  gBS->FreePool(Handles);
}

/**
  This FILTER_FUNCTION checks if a handle corresponds to a PCI display device.
**/
STATIC
BOOLEAN
EFIAPI
IsPciDisplay(IN EFI_HANDLE Handle, IN CONST CHAR16 *ReportText)
{
  EFI_STATUS           Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00           Pci;

  Status = gBS->HandleProtocol(Handle, &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
  if (EFI_ERROR(Status)) {
    //
    // This is not an error worth reporting.
    //
    return FALSE;
  }

  Status = PciIo->Pci.Read(
      PciIo, EfiPciIoWidthUint32, 0 /* Offset */, sizeof Pci / sizeof(UINT32),
      &Pci);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "%a: %s: %r\n", __FUNCTION__, ReportText, Status));
    return FALSE;
  }

  return IS_PCI_DISPLAY(&Pci);
}

/**
  This CALLBACK_FUNCTION attempts to connect a handle non-recursively, asking
  the matching driver to produce all first-level child handles.
**/
STATIC
VOID EFIAPI Connect(IN EFI_HANDLE Handle, IN CONST CHAR16 *ReportText)
{
  EFI_STATUS Status;

  Status = gBS->ConnectController(
      Handle, // ControllerHandle
      NULL,   // DriverImageHandle
      NULL,   // RemainingDevicePath -- produce all children
      FALSE   // Recursive
  );
  DEBUG(
      (EFI_ERROR(Status) ? EFI_D_ERROR : EFI_D_VERBOSE, "%a: %s: %r\n",
       __FUNCTION__, ReportText, Status));
}

/**
  This CALLBACK_FUNCTION retrieves the EFI_DEVICE_PATH_PROTOCOL from the
  handle, and adds it to ConOut and ErrOut.
**/
STATIC
VOID EFIAPI AddOutput(IN EFI_HANDLE Handle, IN CONST CHAR16 *ReportText)
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  DevicePath = DevicePathFromHandle(Handle);
  if (DevicePath == NULL) {
    DEBUG(
        (EFI_D_ERROR, "%a: %s: handle %p: device path not found\n",
         __FUNCTION__, ReportText, Handle));
    return;
  }

  Status = EfiBootManagerUpdateConsoleVariable(ConOut, DevicePath, NULL);
  if (EFI_ERROR(Status)) {
    DEBUG(
        (EFI_D_ERROR, "%a: %s: adding to ConOut: %r\n", __FUNCTION__,
         ReportText, Status));
    return;
  }

  Status = EfiBootManagerUpdateConsoleVariable(ErrOut, DevicePath, NULL);
  if (EFI_ERROR(Status)) {
    DEBUG(
        (EFI_D_ERROR, "%a: %s: adding to ErrOut: %r\n", __FUNCTION__,
         ReportText, Status));
    return;
  }

  DEBUG(
      (EFI_D_VERBOSE, "%a: %s: added to ConOut and ErrOut\n", __FUNCTION__,
       ReportText));
}

STATIC
UINT16
PlatformRegisterFvBootOption(
    CONST EFI_GUID *FileGuid, CHAR16 *Description, UINT32 Attributes)
{
  EFI_STATUS                        Status;
  INTN                              OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION      NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION *    BootOptions;
  UINTN                             BootOptionCount;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_LOADED_IMAGE_PROTOCOL *       LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL *        DevicePath;
  UINT16                            OptionNumber;

  Status = gBS->HandleProtocol(
      gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  ASSERT_EFI_ERROR(Status);

  EfiInitializeFwVolDevicepathNode(&FileNode, FileGuid);
  DevicePath = DevicePathFromHandle(LoadedImage->DeviceHandle);
  ASSERT(DevicePath != NULL);
  DevicePath =
      AppendDevicePathNode(DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  ASSERT(DevicePath != NULL);

  Status = EfiBootManagerInitializeLoadOption(
      &NewOption, LoadOptionNumberUnassigned, LoadOptionTypeBoot, Attributes,
      Description, DevicePath, NULL, 0);
  ASSERT_EFI_ERROR(Status);
  FreePool(DevicePath);

  BootOptions =
      EfiBootManagerGetLoadOptions(&BootOptionCount, LoadOptionTypeBoot);

  OptionIndex =
      EfiBootManagerFindLoadOption(&NewOption, BootOptions, BootOptionCount);

  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable(&NewOption, MAX_UINTN);
    ASSERT_EFI_ERROR(Status);
  }
  OptionNumber = NewOption.OptionNumber;
  EfiBootManagerFreeLoadOption(&NewOption);
  EfiBootManagerFreeLoadOptions(BootOptions, BootOptionCount);
  return OptionNumber;
}

STATIC
VOID GetPlatformOptions(VOID)
{
  EFI_STATUS                      Status;
  EFI_BOOT_MANAGER_LOAD_OPTION *  CurrentBootOptions;
  EFI_BOOT_MANAGER_LOAD_OPTION *  BootOptions;
  EFI_INPUT_KEY *                 BootKeys;
  PLATFORM_BOOT_MANAGER_PROTOCOL *PlatformBootManager;
  UINTN                           CurrentBootOptionCount;
  UINTN                           Index;
  UINTN                           BootCount;

  Status = gBS->LocateProtocol(
      &gPlatformBootManagerProtocolGuid, NULL, (VOID **)&PlatformBootManager);
  if (EFI_ERROR(Status)) {
    return;
  }
  Status = PlatformBootManager->GetPlatformBootOptionsAndKeys(
      &BootCount, &BootOptions, &BootKeys);
  if (EFI_ERROR(Status)) {
    return;
  }
  //
  // Fetch the existent boot options. If there are none, CurrentBootCount
  // will be zeroed.
  //
  CurrentBootOptions =
      EfiBootManagerGetLoadOptions(&CurrentBootOptionCount, LoadOptionTypeBoot);
  //
  // Process the platform boot options.
  //
  for (Index = 0; Index < BootCount; Index++) {
    INTN  Match;
    UINTN BootOptionNumber;

    //
    // If there are any preexistent boot options, and the subject platform boot
    // option is already among them, then don't try to add it. Just get its
    // assigned boot option number so we can associate a hotkey with it. Note
    // that EfiBootManagerFindLoadOption() deals fine with (CurrentBootOptions
    // == NULL) if (CurrentBootCount == 0).
    //
    Match = EfiBootManagerFindLoadOption(
        &BootOptions[Index], CurrentBootOptions, CurrentBootOptionCount);
    if (Match >= 0) {
      BootOptionNumber = CurrentBootOptions[Match].OptionNumber;
    }
    else {
      //
      // Add the platform boot options as a new one, at the end of the boot
      // order. Note that if the platform provided this boot option with an
      // unassigned option number, then the below function call will assign a
      // number.
      //
      Status =
          EfiBootManagerAddLoadOptionVariable(&BootOptions[Index], MAX_UINTN);
      if (EFI_ERROR(Status)) {
        DEBUG(
            (DEBUG_ERROR, "%a: failed to register \"%s\": %r\n", __FUNCTION__,
             BootOptions[Index].Description, Status));
        continue;
      }
      BootOptionNumber = BootOptions[Index].OptionNumber;
    }

    //
    // Register a hotkey with the boot option, if requested.
    //
    if (BootKeys[Index].UnicodeChar == L'\0') {
      continue;
    }

    Status = EfiBootManagerAddKeyOptionVariable(
        NULL, BootOptionNumber, 0, &BootKeys[Index], NULL);
    if (EFI_ERROR(Status)) {
      DEBUG(
          (DEBUG_ERROR, "%a: failed to register hotkey for \"%s\": %r\n",
           __FUNCTION__, BootOptions[Index].Description, Status));
    }
  }
  EfiBootManagerFreeLoadOptions(CurrentBootOptions, CurrentBootOptionCount);
  EfiBootManagerFreeLoadOptions(BootOptions, BootCount);
  FreePool(BootKeys);
}

STATIC
VOID PlatformRegisterOptionsAndKeys(VOID)
{
  EFI_STATUS                   Status;
  EFI_INPUT_KEY                Enter;
  EFI_INPUT_KEY                UP;
  EFI_INPUT_KEY                Esc;
  EFI_BOOT_MANAGER_LOAD_OPTION BootOption;

  GetPlatformOptions();

  //
  // Register ENTER as CONTINUE key
  //
  Enter.ScanCode    = SCAN_NULL;
  Enter.UnicodeChar = CHAR_CARRIAGE_RETURN;
  Status            = EfiBootManagerRegisterContinueKeyOption(0, &Enter, NULL);
  ASSERT_EFI_ERROR(Status);

  //
  // Map UP and ESC to Boot Manager Menu or SimpleInitGUI
  //
  UP.ScanCode     = SCAN_UP;
  UP.UnicodeChar  = CHAR_NULL;
  Esc.ScanCode    = SCAN_ESC;
  Esc.UnicodeChar = CHAR_NULL;
  Status          = EfiBootManagerGetBootManagerMenu(&BootOption);
  ASSERT_EFI_ERROR(Status);
#ifdef ENABLE_SIMPLE_INIT

  //
  // Register Simple Init GUI APP
  //
  UINT16 OptionSimpleInit = PlatformRegisterFvBootOption(
      &gSimpleInitFileGuid, L"Simple Init", LOAD_OPTION_ACTIVE);
  Status = EfiBootManagerAddKeyOptionVariable(
      NULL, (UINT16)OptionSimpleInit, 0, &UP, NULL);
#else
  Status = EfiBootManagerAddKeyOptionVariable(
      NULL, (UINT16)BootOption.OptionNumber, 0, &UP, NULL);
#endif
  ASSERT(Status == EFI_SUCCESS || Status == EFI_ALREADY_STARTED);
}

//
// BDS Platform Functions
//
/**
  Do the platform init, can be customized by OEM/IBV
  Possible things that can be done in PlatformBootManagerBeforeConsole:
  > Update console variable: 1. include hot-plug devices;
  >                          2. Clear ConIn and add SOL for AMT
  > Register new Driver#### or Boot####
  > Register new Key####: e.g.: F12
  > Signal ReadyToLock event
  > Authentication action: 1. connect Auth devices;
  >                        2. Identify auto logon user.
**/
VOID EFIAPI PlatformBootManagerBeforeConsole(VOID)
{
  TraceN986BPlatformBmValue (0x2F0, 0xF520);
  //
  // Signal EndOfDxe PI Event
  //
  EfiEventGroupSignal(&gEfiEndOfDxeEventGroupGuid);

  //
  // Dispatch deferred images after EndOfDxe event.
  //
  EfiBootManagerDispatchDeferredImages();

  //
  // Locate the PCI root bridges and make the PCI bus driver connect each,
  // non-recursively. This will produce a number of child handles with PciIo on
  // them.
  //
  FilterAndProcess(&gEfiPciRootBridgeIoProtocolGuid, NULL, Connect);

  //
  // Find all display class PCI devices (using the handles from the previous
  // step), and connect them non-recursively. This should produce a number of
  // child handles with GOPs on them.
  //
  FilterAndProcess(&gEfiPciIoProtocolGuid, IsPciDisplay, Connect);

  //
  // Now add the device path of all handles with GOP on them to ConOut and
  // ErrOut.
  //
  FilterAndProcess(&gEfiGraphicsOutputProtocolGuid, NULL, AddOutput);

  //
  // Add the hardcoded short-form USB keyboard device path to ConIn.
  //
  EfiBootManagerUpdateConsoleVariable(
      ConIn, (EFI_DEVICE_PATH_PROTOCOL *)&mUsbKeyboard, NULL);

  EFI_HANDLE *              handles;
  UINTN                     NoHandles;
  EFI_DEVICE_PATH_PROTOCOL *devicehandle;
  EFI_STATUS                Status;
  // Use the second input handle when present, or the only one available.
  handles = NULL;
  NoHandles = 0;
  Status = gBS->LocateHandleBuffer(
      ByProtocol, &gEfiSimpleTextInputExProtocolGuid, NULL, &NoHandles,
      &handles);
  TraceN986BPlatformBmValue (0x2F8, (UINT64)Status);
  TraceN986BPlatformBmValue (0x310, NoHandles);
  TraceN986BPlatformBmValue (0x2F0, 0xF521);
  if (!EFI_ERROR(Status) && NoHandles != 0 && handles != NULL) {
    devicehandle = DevicePathFromHandle(handles[NoHandles > 1 ? 1 : 0]);
    if (devicehandle != NULL) {
      EfiBootManagerUpdateConsoleVariable(ConIn, devicehandle, NULL);
    }
    FreePool(handles);
  }
  TraceN986BPlatformBmValue (0x2F0, 0xF522);

  //
  // Add the hardcoded serial console device path to ConIn, ConOut, ErrOut.
  //
  ASSERT(FixedPcdGet8(PcdDefaultTerminalType) == 4);
  CopyGuid(&mSerialConsole.TermType.Guid, &gEfiTtyTermGuid);

  EfiBootManagerUpdateConsoleVariable(
      ConIn, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable(
      ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable(
      ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);

  //
  // Register platform-specific boot options and keyboard shortcuts.
  //
  PlatformRegisterOptionsAndKeys();
  TraceN986BPlatformBmValue (0x2F0, 0xF523);
}

STATIC
VOID HandleCapsules(VOID)
{
  ESRT_MANAGEMENT_PROTOCOL *EsrtManagement;
  EFI_PEI_HOB_POINTERS      HobPointer;
  EFI_CAPSULE_HEADER *      CapsuleHeader;
  BOOLEAN                   NeedReset;
  EFI_STATUS                Status;

  DEBUG((DEBUG_INFO, "%a: processing capsules ...\n", __FUNCTION__));

  Status = gBS->LocateProtocol(
      &gEsrtManagementProtocolGuid, NULL, (VOID **)&EsrtManagement);
  if (!EFI_ERROR(Status)) {
    EsrtManagement->SyncEsrtFmp();
  }

  //
  // Find all capsule images from hob
  //
  HobPointer.Raw = GetHobList();
  NeedReset      = FALSE;
  while ((HobPointer.Raw =
              GetNextHob(EFI_HOB_TYPE_UEFI_CAPSULE, HobPointer.Raw)) != NULL) {
    CapsuleHeader = (VOID *)(UINTN)HobPointer.Capsule->BaseAddress;

    Status = ProcessCapsuleImage(CapsuleHeader);
    if (EFI_ERROR(Status)) {
      DEBUG(
          (DEBUG_ERROR, "%a: failed to process capsule %p - %r\n", __FUNCTION__,
           CapsuleHeader, Status));
      return;
    }

    NeedReset      = TRUE;
    HobPointer.Raw = GET_NEXT_HOB(HobPointer);
  }

  if (NeedReset) {
    DEBUG(
        (DEBUG_WARN, "%a: capsule update successful, resetting ...\n",
         __FUNCTION__));

    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    CpuDeadLoop();
  }
}

#define VERSION_STRING_PREFIX L"Tianocore/EDK2 firmware version "

/**
  Do the platform specific action after the console is ready
  Possible things that can be done in PlatformBootManagerAfterConsole:
  > Console post action:
    > Dynamically switch output mode from 100x31 to 80x25 for certain scenario
    > Signal console ready platform customized event
  > Run diagnostics like memory testing
  > Connect certain devices
  > Dispatch additional option roms
  > Special boot: e.g.: USB boot, enter UI
**/
VOID EFIAPI PlatformBootManagerAfterConsole(VOID)
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
  UINTN                         FirmwareVerLength;
  UINTN                         PosX;
  UINTN                         PosY;
#if defined (N986B_DXE_PROBE)
  EFI_STATUS                    ProbeGopStatus;
  EFI_STATUS                    ProbeConOutGopStatus;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *ProbeGop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *ProbeConOutGop;
#endif

  TraceN986BPlatformBmValue (0x380, 0xF560);
  FirmwareVerLength = StrLen(PcdGetPtr(PcdFirmwareVersionString));

  //
  // Show the splash screen.
  //
  Status = BootLogoEnableLogo();
  TraceN986BPlatformBmValue (0x398, (UINT64)Status);
  TraceN986BPlatformBmValue (0x380, 0xF561);
#if defined (N986B_DXE_PROBE)
  ProbeGop       = NULL;
  ProbeConOutGop = NULL;
  TraceN986BPlatformBmValue (0x388, (UINTN)gST->ConsoleOutHandle);
  TraceN986BPlatformBmValue (0x390, (UINTN)gST->ConOut);
  TraceN986BPlatformBmValue (0x3A8, 0);
  TraceN986BPlatformBmValue (0x3B0, 0);
  TraceN986BPlatformBmValue (0x3B8, 0);
  TraceN986BPlatformBmValue (0x3C0, 0);
  ProbeGopStatus = gBS->LocateProtocol (
                           &gEfiGraphicsOutputProtocolGuid,
                           NULL,
                           (VOID **)&ProbeGop
                           );
  TraceN986BPlatformBmValue (0x3A0, (UINT64)ProbeGopStatus);
  if (!EFI_ERROR (ProbeGopStatus) && (ProbeGop != NULL) &&
      (ProbeGop->Mode != NULL) && (ProbeGop->Mode->Info != NULL)) {
    TraceN986BPlatformBmValue (0x3A8, ProbeGop->Mode->FrameBufferBase);
    TraceN986BPlatformBmValue (0x3B0, ProbeGop->Mode->FrameBufferSize);
    TraceN986BPlatformBmValue (0x3B8, ProbeGop->Mode->Info->HorizontalResolution);
    TraceN986BPlatformBmValue (0x3C0, ProbeGop->Mode->Info->VerticalResolution);
  }
  ProbeConOutGopStatus = EFI_NOT_FOUND;
  if (gST->ConsoleOutHandle != NULL) {
    ProbeConOutGopStatus = gBS->HandleProtocol (
                                  gST->ConsoleOutHandle,
                                  &gEfiGraphicsOutputProtocolGuid,
                                  (VOID **)&ProbeConOutGop
                                  );
  }
  TraceN986BPlatformBmValue (0x3C8, (UINT64)ProbeConOutGopStatus);
  TraceN986BPlatformBmValue (0x380, 0xF562);
#endif
  if (EFI_ERROR(Status)) {
    if (FirmwareVerLength > 0) {
      Print(VERSION_STRING_PREFIX L"%s\n", PcdGetPtr(PcdFirmwareVersionString));
    }
#ifdef ENABLE_SIMPLE_INIT
    Print(L"Press any side button for SimpleInitGUI");
#else
    Print(L"Press any side button for Boot Options");
#endif
  }
  else if (FirmwareVerLength > 0) {
    Status = gBS->HandleProtocol(
        gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid,
        (VOID **)&GraphicsOutput);
    if (!EFI_ERROR(Status)) {
      PosX = (GraphicsOutput->Mode->Info->HorizontalResolution -
              (StrLen(VERSION_STRING_PREFIX) + FirmwareVerLength) *
                  EFI_GLYPH_WIDTH) /
             2;
      PosY = 0;

      PrintXY(
          PosX, PosY, NULL, NULL, VERSION_STRING_PREFIX L"%s",
          PcdGetPtr(PcdFirmwareVersionString));
    }
  }

  //
  // Connect the rest of the devices.
  //
  EfiBootManagerConnectAll();
  TraceN986BPlatformBmValue (0x380, 0xF563);

  //
  // On ARM, there is currently no reason to use the phased capsule
  // update approach where some capsules are dispatched before EndOfDxe
  // and some are dispatched after. So just handle all capsules here,
  // when the console is up and we can actually give the user some
  // feedback about what is going on.
  //
  HandleCapsules();

  //
  // Enumerate all possible boot options.
  //
  EfiBootManagerRefreshAllBootOption();

  //
#if defined (N986B_DXE_PROBE)
#if defined (N986B_RECOVERY_BOOT)
#if defined (N986B_RECOVERY_UI_HIGH_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B high RAM TWRP UI candidate", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_UI_HIGH_DUAL_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B high RAM TWRP UI dual blacklist candidate", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_UI_HIGH_DUAL_ECT_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B high RAM TWRP UI ECT branch candidate", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_UI_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B RAM TWRP UI candidate", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_ECT_HOLD_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated ECT RAM hold diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_ECT_BRANCH_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated ECT RAM Linux diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_TRIPLE_BLACKLIST_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated RAM Linux triple blacklist diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_DUAL_BLACKLIST_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated RAM Linux dual blacklist diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_BRANCH_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated RAM Linux diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_RELOCATED_HOLD_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B relocated RAM hold diagnostic", LOAD_OPTION_ACTIVE);
#elif defined (N986B_RECOVERY_HOLD_BOOT)
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B RAM recovery hold diagnostic", LOAD_OPTION_ACTIVE);
#else
  PlatformRegisterFvBootOption(
      &mN986BRecoveryFileGuid, L"N986B RAM recovery diagnostic", LOAD_OPTION_ACTIVE);
#endif
#else
  // Prepare the RAM stream first. Handoff is compile-time disabled in this
  // diagnostic FD; the application returns to the UEFI Shell option below.
  PlatformRegisterFvBootOption(
      &mN986BXnuLoaderFileGuid, L"N986B XNU RAM preparation", LOAD_OPTION_ACTIVE);
#endif
#endif

  // Register UEFI Shell
  //
  PlatformRegisterFvBootOption(
      &gUefiShellFileGuid, L"UEFI Shell", LOAD_OPTION_ACTIVE);

#ifdef ENABLE_LINUX_SIMPLE_MASS_STORAGE
  //
  // Register Built-in Linux Kernel
  //
  PlatformRegisterFvBootOption(
      &gLinuxSimpleMassStorageGuid, L"USB Attached SCSI (UAS) Storage", LOAD_OPTION_ACTIVE);
#endif

#ifdef AB_SLOTS_SUPPORT
  //
  // Register Switch Slots App
  //
  PlatformRegisterFvBootOption(
      &gSwitchSlotsAppFileGuid, L"Reboot to other slot", LOAD_OPTION_ACTIVE);
#endif

  PlatformSetup();
}

/**
  This function is called each second during the boot manager waits the
  timeout.

  @param TimeoutRemain  The remaining timeout.
**/
VOID EFIAPI PlatformBootManagerWaitCallback(UINT16 TimeoutRemain)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION White;
  UINT16                              Timeout;
  EFI_STATUS                          Status;

  Timeout = PcdGet16(PcdPlatformBootTimeOut);

  if (Timeout != 0 && TimeoutRemain <= 0) {
    gST->ConOut->ClearScreen(gST->ConOut);
    BootLogoEnableLogo ();
    return;
  }

  Black.Raw = 0x00000000;
  White.Raw = 0x00FFFFFF;

  Status = BootLogoUpdateProgress(
      White.Pixel, Black.Pixel,
#ifdef ENABLE_SIMPLE_INIT
      L"Press any side button for SimpleInitGUI",
#else
      L"Press any side button for Boot Options",
#endif
      White.Pixel, (Timeout - TimeoutRemain) * 100 / Timeout, 0);
  if (EFI_ERROR(Status)) {
    Print(L".");
  }
}

/**
  The function is called when no boot option could be launched,
  including platform recovery options and options pointing to applications
  built into firmware volumes.

  If this function returns, BDS attempts to enter an infinite loop.
**/
VOID EFIAPI PlatformBootManagerUnableToBoot(VOID) { return; }
