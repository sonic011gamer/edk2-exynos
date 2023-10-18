[Defines]
  PLATFORM_NAME                  = s6
  PLATFORM_GUID                  = e940fcdd-48c9-4c1f-af7c-523b972808b8
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/Samsung/exynos7420/exynos7420.fdf
  DEVICE_DXE_FV_COMPONENTS       = Platform/Samsung/exynos7420/exynos7420.fdf.inc
  BROKEN_CNTFRQ_EL0              = 1
  IMPLEMENTS_CUSTOM_RESETLIB	 = 1

!include Platform/Samsung/exynos7420/exynos7420.dsc

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DENABLE_SIMPLE_INIT -DBROKEN_CNTFRQ_EL0=$(BROKEN_CNTFRQ_EL0)

[PcdsFixedAtBuild.common]
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferWidth|1440
  gSamsungTokenSpaceGuid.PcdMipiFrameBufferHeight|2560

  # Simple Init
  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|350

  gRenegadePkgTokenSpaceGuid.PcdDeviceVendor|"Samsung"
  gRenegadePkgTokenSpaceGuid.PcdDeviceProduct|"Galaxy S6/S6 edge"
  gRenegadePkgTokenSpaceGuid.PcdDeviceCodeName|"G920F"
