add_executable(cirrus_initrd initrd.c)
target_include_directories(cirrus_initrd PRIVATE "${PROJECT_SOURCE_DIR}/boot/cirrus-boot/include")
target_link_libraries(cirrus_initrd PRIVATE cirrus_warnings)
set_target_properties(cirrus_initrd PROPERTIES OUTPUT_NAME cirrus-initrd)
