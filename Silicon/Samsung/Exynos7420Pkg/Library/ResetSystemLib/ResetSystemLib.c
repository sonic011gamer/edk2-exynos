/** @file
  ResetSystemLib implementation using PSCI calls
  Copyright (c) 2017 - 2018, Linaro Ltd. All rights reserved.<BR>
  Copyright (c) 2019, Intel Corporation. All rights reserved.<BR>
  Copyright (c) 2022, Arm Limited. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <PiDxe.h>

#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/CacheMaintenanceLib.h>



#define PMU_BASE (0x105C0000)
#define SWRESET_OFFSET (0x400)
#define PS_HOLD_CONTROL (0x330C)

/**
  This function causes a system-wide reset (cold reset), in which
  all circuitry within the system returns to its initial state. This type of reset
  is asynchronous to system operation and operates without regard to
  cycle boundaries.
  If this function returns, it means that the system does not support cold reset.
**/
VOID
EFIAPI
ResetCold (
  VOID
  )
{
  DEBUG ((EFI_D_ERROR, "ResetSystemLib: Cold reset!"));
  MmioWrite32 ((PMU_BASE + SWRESET_OFFSET), 0x01);
  while ((MmioRead32(PMU_BASE + SWRESET_OFFSET)) != 0x1);
}

/**
  This function causes a system-wide initialization (warm reset), in which all processors
  are set to their initial state. Pending cycles are not corrupted.
  If this function returns, it means that the system does not support warm reset.
**/
VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  DEBUG ((EFI_D_ERROR, "ResetSystemLib: Warm Reset!"));
  ResetCold(); // fix later
}

/**
  This function causes the system to enter a power state equivalent
  to the ACPI G2/S5 or G3 states.
  If this function returns, it means that the system does not support shutdown reset.
**/
VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  DEBUG ((EFI_D_ERROR, "ResetSystemLib: Shutting down!"));
  MmioWrite32(PMU_BASE + PS_HOLD_CONTROL, MmioRead32(PMU_BASE + PS_HOLD_CONTROL) & 0xFFFFFEFF);
}

/**
  This function causes a systemwide reset. The exact type of the reset is
  defined by the EFI_GUID that follows the Null-terminated Unicode string passed
  into ResetData. If the platform does not recognize the EFI_GUID in ResetData
  the platform must pick a supported reset type to perform.The platform may
  optionally log the parameters from any non-normal reset that occurs.
  @param[in]  DataSize   The size, in bytes, of ResetData.
  @param[in]  ResetData  The data buffer starts with a Null-terminated string,
                         followed by the EFI_GUID.
**/
VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  //	if (!strcmp(cmd, "charge")) {
	//	__raw_writel(REBOOT_MODE_CHARGE, addr);
	//} else if (!strcmp(cmd, "fastboot") || !strcmp(cmd, "fb")) {
	//	__raw_writel(REBOOT_MODE_FASTBOOT, addr);
	//} else if (!strcmp(cmd, "bootloader") || !strcmp(cmd, "bl")) {
	//	__raw_writel(REBOOT_MODE_BOOTLOADER, addr);
	//} else if (!strcmp(cmd, "recovery")) {
	//	__raw_writel(REBOOT_MODE_RECOVERY, addr);
	//}
}

/**
  The ResetSystem function resets the entire platform.
  @param[in] ResetType      The type of reset to perform.
  @param[in] ResetStatus    The status code for the reset.
  @param[in] DataSize       The size, in bytes, of ResetData.
  @param[in] ResetData      For a ResetType of EfiResetCold, EfiResetWarm, or EfiResetShutdown
                            the data buffer starts with a Null-terminated string, optionally
                            followed by additional binary data. The string is a description
                            that the caller may use to further indicate the reason for the
                            system reset.
**/
VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;

    case EfiResetCold:
      ResetCold ();
      break;

    case EfiResetShutdown:
      ResetShutdown ();
      return;

    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      return;

    default:
      return;
  }
}
