function platform_build_bootimg(){
        python3 "${ROOTDIR}/tools/mkbootimg.py" \
                --tags_offset 0x00000100 --second_offset 0xf0000000 --ramdisk_offset 0x01000000 --pagesize 2048 --os_version "13.0.0" --os_patch_level "2023-07" --kernel_offset 0x00008000 --header_version 2  --dtb_offset 0x00000000 --cmdline "buildvariant=userdebug" --board "" --base 0x10000000 --ramdisk "Platform/Samsung/exynos990/dummyramdisk" --dtb "Platform/Samsung/exynos990/FdtBlob_compat/${DEVICE}.dtb" --kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
                -o "${WORKSPACE}/boot.img" \
                ||return "$?"
    cd "${WORKSPACE}"
        tar -c \
        boot.img \
        -f boot.tar \
        ||return "$?"
}
