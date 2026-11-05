if(NOT SLOT MATCHES "^[ab]$" OR NOT HEADER MATCHES "^[012]$")
    message(FATAL_ERROR "Use slot a|b and Android header version 0|1|2")
endif()
if(NOT EXISTS "${DTB}")
    message(FATAL_ERROR "Set CIRRUS_DTB to the device's DTB before building boot-image")
endif()
find_program(GZIP_TOOL NAMES gzip REQUIRED)
get_filename_component(out_dir "${OUTPUT}" DIRECTORY)
set(blob "${out_dir}/loader.gz")
execute_process(COMMAND "${GZIP_TOOL}" -n -9 -c "${LOADER}"
    OUTPUT_FILE "${blob}" COMMAND_ERROR_IS_FATAL ANY)
if(HEADER LESS 2)
    # Appended DTB follows the gzip stream in the ABL kernel payload.
    find_program(CAT_TOOL NAMES cat REQUIRED)
    set(payload "${out_dir}/loader-dtb.gz")
    execute_process(COMMAND "${CAT_TOOL}" "${blob}" "${DTB}"
        OUTPUT_FILE "${payload}" COMMAND_ERROR_IS_FATAL ANY)
    set(dtb_argument "")
else()
    set(payload "${blob}")
    set(dtb_argument "${DTB}")
endif()
execute_process(COMMAND "${TOOL}" pack "${OUTPUT}"
    --page-size "${PAGE_SIZE}" --header-version "${HEADER}"
    --base 0x80000000 --kernel-offset 0x8000 --ramdisk-offset 0x02000000
    --second-offset 0x00f00000 --tags-offset 0x01e00000 --cmdline "${CMDLINE}"
    "${payload}" "${INITRD}" ${dtb_argument} COMMAND_ERROR_IS_FATAL ANY)
