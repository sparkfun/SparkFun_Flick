# Bootloader hook for 4 MB boards: installs a staged Baseline update (bsu_stage.c).
# bsk copies bsu_inflate.c/.h and bsu_format.h here from baseline/src/bsu.
idf_component_register(SRCS "bsu_stage.c" "bsu_inflate.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bootloader_support spi_flash hal log)
# bootloader_sha.h is in bootloader_support's private headers.
idf_component_get_property(bootloader_support_dir bootloader_support COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${bootloader_support_dir}/private_include")
