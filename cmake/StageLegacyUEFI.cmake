set(esp "${ESP}")
file(MAKE_DIRECTORY "${esp}/EFI/BOOT" "${esp}/EFI/KUMO/kernel")
file(COPY_FILE "${BUILD}/kumo/aarch64-unknown-uefi/release/niji-uefi.efi"
    "${esp}/EFI/BOOT/BOOTAA64.EFI" ONLY_IF_DIFFERENT)
file(COPY_FILE "${BUILD}/kumo/aarch64-unknown-none/release/kumo-kernel"
    "${esp}/EFI/KUMO/kernel/kumo-kernel.elf" ONLY_IF_DIFFERENT)
file(COPY_FILE "${BUILD}/initrd.img" "${esp}/EFI/KUMO/initrd.img" ONLY_IF_DIFFERENT)
file(WRITE "${esp}/EFI/KUMO/nijigumo.conf"
    "kernel = \\EFI\\KUMO\\kernel\\kumo-kernel.elf\ninitrd = \\EFI\\KUMO\\initrd.img\ndtb = firmware\nboard = orange-pi-5-plus\n")
message(STATUS "Staged temporary upstream UEFI adapter: ${esp}")
