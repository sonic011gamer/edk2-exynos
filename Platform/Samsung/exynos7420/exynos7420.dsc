## @file
#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018 - 2019, Bingxing Wang. All rights reserved.
#  Copyright (c) 2022, Xilin Wu. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################

[Defines]
  SOC_PLATFORM            = exynos7420
  USE_PHYSICAL_TIMER      = FALSE

!include Silicon/Samsung/ExynosPkg/ExynosCommonDsc.inc

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0xBE800000

  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x40C40000     # CPU Vectors

  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|24000000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|13
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|14
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|11
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|10

  gArmTokenSpaceGuid.PcdGicDistributorBase|0x11001000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x11002000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x11004000

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00000850
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x40C00000      # UEFI Stack
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000      # 256K stack
  #gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|44

  gSamsungTokenSpaceGuid.PcdUefiMemPoolBase|0x40D00000         # DXE Heap base address
  gSamsungTokenSpaceGuid.PcdUefiMemPoolSize|0x07000000         # UefiMemorySize, DXE heap size
  
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferAddress|0xe2a00000
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferWidth|1440
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferHeight|2560
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferVisibleWidth|1440
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferVisibleHeight|2560

  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # SimpleInit
  #
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0x80000000
  gSimpleInitTokenSpaceGuid.PcdLoggerdUseConsole|FALSE

[LibraryClasses.common]
  KeypadDeviceImplLib|Silicon/Samsung/Exynos7420Pkg/Library/KeypadDeviceImplLib/KeypadDeviceImplLib.inf
  PlatformMemoryMapLib|Silicon/Samsung/Exynos7420Pkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  PlatformPeiLib|Silicon/Samsung/Exynos7420Pkg/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|Silicon/Samsung/Exynos7420Pkg/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  HwResetSystemLib|Silicon/Samsung/Exynos7420Pkg/Library/ResetSystemLib/ResetSystemLib.inf
  ResetSystemLib|Silicon/Samsung/Exynos7420Pkg/Library/ResetSystemLib/ResetSystemLib.inf
  MsPlatformDevicesLib|Silicon/Samsung/Exynos7420Pkg/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf
  SOCSmbiosInfoLib|Silicon/Samsung/Exynos7420Pkg/Library/SOCSmbiosInfoLib/SOCSmbiosInfoLib.inf
