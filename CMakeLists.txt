cmake_minimum_required(VERSION 3.24)
project(Cirrus VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(cirrus_warnings INTERFACE)
target_compile_options(cirrus_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall;-Wextra;-Werror>)

if(CMAKE_CROSSCOMPILING)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        message(FATAL_ERROR "CIRRUS currently supports the aarch64 target only")
    endif()
    add_library(cirrus_freestanding INTERFACE)
    target_compile_options(cirrus_freestanding INTERFACE
        -ffreestanding -fno-stack-protector -fno-pic -fno-builtin
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>)
    target_link_options(cirrus_freestanding INTERFACE -nostdlib -static)
    add_subdirectory(userland)
    add_subdirectory(boot/cirrus-boot)
    return()
endif()

include(CTest)
add_library(cirrus_ipc STATIC userland/ipc_ring.c)
target_include_directories(cirrus_ipc PUBLIC include)
target_link_libraries(cirrus_ipc PRIVATE cirrus_warnings)
add_subdirectory(boot/cirrus-boot)
add_subdirectory(tools)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(CIRRUS_BUILD_SYSTEM "Build the KUMO kernel and ARM64 Cirrus images" OFF)
if(CIRRUS_BUILD_SYSTEM)
    include(cmake/System.cmake)
endif()
