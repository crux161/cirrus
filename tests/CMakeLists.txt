add_executable(c_ipc_ring_host c_ipc_ring_host.c)
target_link_libraries(c_ipc_ring_host PRIVATE cirrus_ipc cirrus_warnings)
add_test(NAME ipc-ring COMMAND c_ipc_ring_host)

add_library(c_abi_check OBJECT c_abi/kumo_trap_static_asserts.c)
target_include_directories(c_abi_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(c_abi_check PRIVATE cirrus_warnings)
configure_file(c_abi/kumo_trap_static_asserts.c "${CMAKE_CURRENT_BINARY_DIR}/abi.cpp" COPYONLY)
add_library(cxx_abi_check OBJECT "${CMAKE_CURRENT_BINARY_DIR}/abi.cpp")
target_include_directories(cxx_abi_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cxx_abi_check PRIVATE cirrus_warnings)

find_program(CIRRUS_DTC NAMES dtc REQUIRED)
set(boot_fixtures "")
foreach(name sdm845_test sdm845_test_reversed)
    set(dtb "${CMAKE_CURRENT_BINARY_DIR}/${name}.dtb")
    add_custom_command(OUTPUT "${dtb}"
        COMMAND "${CIRRUS_DTC}" -I dts -O dtb -o "${dtb}"
            "${PROJECT_SOURCE_DIR}/boot/cirrus-boot/tests/${name}.dts"
        DEPENDS "${PROJECT_SOURCE_DIR}/boot/cirrus-boot/tests/${name}.dts" VERBATIM)
    list(APPEND boot_fixtures "${dtb}")
endforeach()
add_custom_target(boot-fixtures ALL DEPENDS ${boot_fixtures})
add_executable(cirrus_boot_host cirrus_boot_host.c)
target_link_libraries(cirrus_boot_host PRIVATE cirrus_boot cirrus_warnings)
add_dependencies(cirrus_boot_host boot-fixtures)
foreach(dtb IN LISTS boot_fixtures)
    get_filename_component(name "${dtb}" NAME_WE)
    add_test(NAME "boot-${name}" COMMAND cirrus_boot_host "${dtb}" synthetic)
endforeach()
add_test(NAME boot-orangepi COMMAND cirrus_boot_host
    "${PROJECT_SOURCE_DIR}/src/rk3588-orangepi-5-plus.dtb" orangepi)
if(EXISTS "${PROJECT_SOURCE_DIR}/resources/linux/dtb/sdm845-oneplus-enchilada.dtb")
    add_test(NAME boot-oneplus-real COMMAND cirrus_boot_host
        "${PROJECT_SOURCE_DIR}/resources/linux/dtb/sdm845-oneplus-enchilada.dtb" real)
endif()

add_executable(initrd_roundtrip initrd_roundtrip.c)
target_link_libraries(initrd_roundtrip PRIVATE cirrus_boot cirrus_warnings)
add_test(NAME initrd-tool COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:cirrus_initrd>" "-DREADER=$<TARGET_FILE:initrd_roundtrip>"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/initrd-test" -P "${CMAKE_CURRENT_SOURCE_DIR}/initrd_tool.cmake")
add_test(NAME bootimg-tool COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:cirrus_bootimg>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/bootimg-test"
    "-DDTB=${CMAKE_CURRENT_BINARY_DIR}/sdm845_test.dtb" -P "${CMAKE_CURRENT_SOURCE_DIR}/bootimg_tool.cmake")

add_test(NAME language-boundary COMMAND "${PROJECT_SOURCE_DIR}/scripts/check-language-boundary.sh")
