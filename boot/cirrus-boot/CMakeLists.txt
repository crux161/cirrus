set(boot_sources src/boot_image.c src/boot_plan.c src/bootinfo.c
    src/initrd.c src/elf.c src/console.c)

add_library(cirrus_boot STATIC ${boot_sources})
target_include_directories(cirrus_boot PUBLIC include)
target_link_libraries(cirrus_boot PRIVATE cirrus_warnings)

if(CMAKE_CROSSCOMPILING)
    option(CIRRUS_LOADER_QEMU "Enable the QEMU test DTB fallback" ON)
    set(CIRRUS_LOADER_BASE "0x40080000" CACHE STRING "Loader physical load address")
    target_link_libraries(cirrus_boot PRIVATE cirrus_freestanding)
    target_compile_options(cirrus_boot PRIVATE -mgeneral-regs-only)
    add_executable(cirrus_loader src/entry.S src/loader.c src/support.c)
    target_link_libraries(cirrus_loader PRIVATE cirrus_boot cirrus_freestanding cirrus_warnings)
    target_compile_options(cirrus_loader PRIVATE -mgeneral-regs-only)
    if(CIRRUS_LOADER_QEMU)
        target_compile_definitions(cirrus_loader PRIVATE CIRRUS_LOADER_TEST)
    endif()
    target_link_options(cirrus_loader PRIVATE
        "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/loader.ld"
        "-Wl,--defsym,LOADER_BASE=${CIRRUS_LOADER_BASE}")
    set_target_properties(cirrus_loader PROPERTIES
        OUTPUT_NAME loader SUFFIX .elf RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/loader.ld")
    find_program(CIRRUS_OBJCOPY NAMES llvm-objcopy REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/loader.bin"
        COMMAND "${CIRRUS_OBJCOPY}" -O binary $<TARGET_FILE:cirrus_loader> "${CMAKE_BINARY_DIR}/loader.bin"
        DEPENDS cirrus_loader VERBATIM)
    add_custom_target(cirrus_loader_binary ALL DEPENDS "${CMAKE_BINARY_DIR}/loader.bin")
else()
    add_executable(cirrus_bootimg tools/bootimg.c)
    target_link_libraries(cirrus_bootimg PRIVATE cirrus_boot cirrus_warnings)
    set_target_properties(cirrus_bootimg PROPERTIES OUTPUT_NAME cirrus-bootimg)
endif()
