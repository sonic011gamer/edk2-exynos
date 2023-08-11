function platform_build_kernel(){
	cat \
		"${ROOTDIR}/tools/BootShim/BootShim.bin" \
		"${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd" \
		> "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" \
		||return "$?"

	cp "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" "${WORKSPACE}/uefi-${DEVICE}-kernel"
}

function platform_build_bootimg(){
	python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		-o "${WORKSPACE}/boot.img" \
		||return "$?"
    cd "${WORKSPACE}"
	tar -c \
	boot.img \
	-f boot.tar \
	||return "$?"
}
