include(ExternalProject)
find_program(CIRRUS_CARGO NAMES cargo REQUIRED)
set(CIRRUS_BOARD "qemu-virt-aarch64" CACHE STRING "qemu-virt-aarch64, oneplus-enchilada or oneplus-fajita")
set_property(CACHE CIRRUS_BOARD PROPERTY STRINGS qemu-virt-aarch64 oneplus-enchilada oneplus-fajita)
if(CIRRUS_BOARD STREQUAL "qemu-virt-aarch64")
    set(loader_base 0x40080000)
    set(loader_qemu ON)
elseif(CIRRUS_BOARD STREQUAL "oneplus-enchilada" OR CIRRUS_BOARD STREQUAL "oneplus-fajita")
    set(loader_base 0x80008000)
    set(loader_qemu OFF)
else()
    message(FATAL_ERROR "Unsupported CIRRUS_BOARD: ${CIRRUS_BOARD}")
endif()

set(arm_build "${CMAKE_BINARY_DIR}/aarch64")
ExternalProject_Add(cirrus_aarch64
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${arm_build}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${PROJECT_SOURCE_DIR}/cmake/toolchains/aarch64-clang.cmake"
        -DCMAKE_BUILD_TYPE=Release
        "-DCIRRUS_LOADER_BASE=${loader_base}" "-DCIRRUS_LOADER_QEMU=${loader_qemu}"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${arm_build}/loader.elf" "${arm_build}/loader.bin"
        "${arm_build}/c_smoke.elf" "${arm_build}/cxx_smoke.elf"
    INSTALL_COMMAND "")

# Cargo owns only the vendored KUMO closure. Invoke it each time so Cargo can
# notice upstream source changes; do not treat a previously built ELF as current.
set(kumo_target "${CMAKE_BINARY_DIR}/kumo")
# This directory is wholly generated. Cargo's clean command requires its
# cache marker before removing a custom target directory.
file(MAKE_DIRECTORY "${kumo_target}")
file(WRITE "${kumo_target}/CACHEDIR.TAG"
    "Signature: 8a477f597d28d172789f06886806bc55\n# Generated KUMO build cache, owned by Cirrus CMake.\n")
set(kumo_elf "${kumo_target}/aarch64-unknown-none/release/kumo-kernel")
add_custom_target(kumo_kernel
    COMMAND "${CMAKE_COMMAND}" -E env KUMO_CONSOLE_LOGLEVEL=7
        "${CIRRUS_CARGO}" build --locked --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
        --target-dir "${kumo_target}" -p kernel --target aarch64-unknown-none --release
    BYPRODUCTS "${kumo_elf}" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
add_custom_target(cirrus_image ALL
    COMMAND $<TARGET_FILE:cirrus_initrd> "${CMAKE_BINARY_DIR}/initrd.img"
        boot/kumo-kernel.elf "${kumo_elf}" bin/sora "${arm_build}/c_smoke.elf"
    DEPENDS kumo_kernel cirrus_aarch64 cirrus_initrd VERBATIM)

if(loader_qemu)
    add_custom_target(qemu-smoke
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/qemu-session.sh" "${CMAKE_BINARY_DIR}" --check
        DEPENDS cirrus_image USES_TERMINAL VERBATIM)
    add_custom_target(qemu
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/qemu-session.sh" "${CMAKE_BINARY_DIR}" --stdio
        DEPENDS cirrus_image USES_TERMINAL VERBATIM)
    add_custom_target(qemu-cxx-smoke
        COMMAND $<TARGET_FILE:cirrus_initrd> "${CMAKE_BINARY_DIR}/initrd-cxx.img"
            boot/kumo-kernel.elf "${kumo_elf}" bin/sora "${arm_build}/cxx_smoke.elf"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/qemu-session.sh" "${CMAKE_BINARY_DIR}" --check --cxx
        DEPENDS cirrus_image USES_TERMINAL VERBATIM)
else()
    set(CIRRUS_DTB "" CACHE FILEPATH "Device DTB (required for boot-image packaging)")
    set(CIRRUS_BOOT_SLOT "a" CACHE STRING "Output slot label: a or b")
    set(CIRRUS_BOOT_HEADER_VERSION "1" CACHE STRING "Android boot header: 0, 1 or 2")
    set(CIRRUS_BOOT_PAGE_SIZE "4096" CACHE STRING "Android boot image page size")
    set(CIRRUS_BOOT_CMDLINE "console=ttyMSM0,115200n8" CACHE STRING "Android boot command line")
    add_custom_target(boot-image
        COMMAND "${CMAKE_COMMAND}"
            "-DTOOL=$<TARGET_FILE:cirrus_bootimg>" "-DLOADER=${arm_build}/loader.bin"
            "-DINITRD=${CMAKE_BINARY_DIR}/initrd.img" "-DDTB=${CIRRUS_DTB}"
            "-DOUTPUT=${CMAKE_BINARY_DIR}/boot_${CIRRUS_BOOT_SLOT}.img"
            "-DSLOT=${CIRRUS_BOOT_SLOT}" "-DHEADER=${CIRRUS_BOOT_HEADER_VERSION}"
            "-DPAGE_SIZE=${CIRRUS_BOOT_PAGE_SIZE}" "-DCMDLINE=${CIRRUS_BOOT_CMDLINE}"
            -P "${PROJECT_SOURCE_DIR}/cmake/BootImage.cmake"
        DEPENDS cirrus_image cirrus_bootimg USES_TERMINAL VERBATIM)
endif()

# Temporary, opt-in upstream Rust firmware adapter for existing Orange Pi
# hardware. It never participates in the native mobile/QEMU boot path.
option(CIRRUS_LEGACY_UEFI "Expose the temporary upstream UEFI staging target" OFF)
if(CIRRUS_LEGACY_UEFI)
    set(CIRRUS_LEGACY_ESP_DIR "${CMAKE_BINARY_DIR}/esp/orange-pi-5-plus" CACHE PATH "Temporary UEFI ESP output")
    add_custom_target(legacy-uefi-esp
        COMMAND "${CIRRUS_CARGO}" build --locked --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml"
            --target-dir "${kumo_target}" -p niji-uefi --target aarch64-unknown-uefi --release
        COMMAND "${CMAKE_COMMAND}" "-DBUILD=${CMAKE_BINARY_DIR}" "-DESP=${CIRRUS_LEGACY_ESP_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/StageLegacyUEFI.cmake"
        DEPENDS cirrus_image WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
endif()
