file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/kernel" "kernel fixture")
file(WRITE "${WORK}/ramdisk" "ramdisk fixture")
foreach(version 0 1 2)
    set(dtb_argument "")
    if(version EQUAL 2)
        set(dtb_argument "${DTB}")
    endif()
    execute_process(COMMAND "${TOOL}" pack "${WORK}/boot.img"
        --page-size 4096 --header-version "${version}" --base 0x80000000
        --kernel-offset 0x8000 --ramdisk-offset 0x02000000 --tags-offset 0x01e00000
        --cmdline console=ttyMSM0,115200n8 "${WORK}/kernel" "${WORK}/ramdisk" ${dtb_argument}
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${TOOL}" info "${WORK}/boot.img"
        OUTPUT_VARIABLE info COMMAND_ERROR_IS_FATAL ANY)
    foreach(expected "header_version ${version}" "kernel_addr 0x80008000"
            "ramdisk_addr 0x82000000" "cmdline console=ttyMSM0,115200n8")
        string(FIND "${info}" "${expected}" found)
        if(found LESS 0)
            message(FATAL_ERROR "boot image missing ${expected}: ${info}")
        endif()
    endforeach()
endforeach()
