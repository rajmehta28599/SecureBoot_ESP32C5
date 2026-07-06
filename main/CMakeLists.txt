# The demo only needs the bootloader_support component for the runtime status
# queries (esp_secure_boot_enabled / esp_flash_encryption_enabled). The core
# components (freertos, log, esp_hw_support) are always available, so we do not
# need to list them explicitly.
idf_component_register(SRCS "secure_boot_demo_main.c"
                       PRIV_REQUIRES bootloader_support
                       INCLUDE_DIRS ".")
