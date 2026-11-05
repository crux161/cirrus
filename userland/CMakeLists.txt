add_library(cirrus_runtime STATIC runtime.c trap.S ipc_ring.c)
target_include_directories(cirrus_runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(cirrus_runtime PUBLIC cirrus_freestanding PRIVATE cirrus_warnings)
add_library(Cirrus::Runtime ALIAS cirrus_runtime)

# Each userspace executable owns its entry point and linker layout.
function(cirrus_add_program name)
    add_executable(${name} ${ARGN} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/crt0.S")
    target_link_libraries(${name} PRIVATE Cirrus::Runtime cirrus_warnings)
    target_link_options(${name} PRIVATE "-Wl,-T,${CMAKE_CURRENT_FUNCTION_LIST_DIR}/user.ld")
    set_target_properties(${name} PROPERTIES SUFFIX .elf
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LINK_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/user.ld")
endfunction()

cirrus_add_program(c_smoke "${PROJECT_SOURCE_DIR}/tests/c_smoke.c")
cirrus_add_program(cxx_smoke "${PROJECT_SOURCE_DIR}/tests/cxx_smoke.cpp")
