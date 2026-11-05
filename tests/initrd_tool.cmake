file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/kernel" "kernel fixture")
file(WRITE "${WORK}/empty" "")
foreach(name first second)
    execute_process(COMMAND "${TOOL}" "${WORK}/${name}.img"
        boot/kumo-kernel.elf "${WORK}/kernel" bin/sora "${WORK}/empty"
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK}/first.img" "${WORK}/second.img"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${READER}" "${WORK}/first.img" COMMAND_ERROR_IS_FATAL ANY)
foreach(path "../bad" "/absolute" "bin//bad" "bin/./bad" "bin/../bad")
    execute_process(COMMAND "${TOOL}" "${WORK}/invalid.img" "${path}" "${WORK}/kernel"
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if(result EQUAL 0)
        message(FATAL_ERROR "initrd accepted invalid path: ${path}")
    endif()
endforeach()
execute_process(COMMAND "${TOOL}" "${WORK}/duplicate.img"
    bin/sora "${WORK}/kernel" bin/sora "${WORK}/empty"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "initrd accepted duplicate paths")
endif()
