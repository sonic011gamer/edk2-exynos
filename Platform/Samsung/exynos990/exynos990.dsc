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
  SOC_PLATFORM            = exynos990
  USE_PHYSICAL_TIMER      = FALSE

!include Silicon/Samsung/ExynosPkg/ExynosCommonDsc.inc

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000         # Starting address
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x300000000         # Limit to 12GB Size here

  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x80C40000     # CPU Vectors
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|27300000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|19
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|20
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x10100000

  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x10102000

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00000850
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x80C00000      # UEFI Stack
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000      # 256K stack
  #gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|44

  gSamsungTokenSpaceGuid.PcdUefiMemPoolBase|0x80C50000         # DXE Heap base address
  gSamsungTokenSpaceGuid.PcdUefiMemPoolSize|0x0F3B0000         # UefiMemorySize, DXE heap size
  
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferAddress|0xf1000000

  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|3

  #
  # SimpleInit
  #
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0x80000000
  gSimpleInitTokenSpaceGuid.PcdLoggerdUseConsole|FALSE

[LibraryClasses.common]
  PlatformMemoryMapLib|Silicon/Samsung/Exynos990Pkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  PlatformPeiLib|Silicon/Samsung/Exynos990Pkg/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|Silicon/Samsung/Exynos990Pkg/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  MsPlatformDevicesLib|Silicon/Samsung/Exynos990Pkg/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf
  SOCSmbiosInfoLib|Silicon/Samsung/Exynos990Pkg/Library/SOCSmbiosInfoLib/SOCSmbiosInfoLib.inf
