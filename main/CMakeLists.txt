# Components the STEP report pulls in:
#   bootloader_support - esp_secure_boot_enabled(), esp_get_flash_encryption_mode()
#   efuse              - esp_efuse_* queries and the ESP_EFUSE_* field table.
#                        bootloader_support requires efuse only PRIVATEly, so it
#                        is NOT propagated here - main must list it itself.
#   esp_app_format     - esp_app_get_description(), esp_app_get_elf_sha256()
#   app_update         - esp_ota_get_running_partition()
#   esp_partition      - esp_partition_t fields
#   spi_flash          - esp_flash_get_size() / esp_flash_get_physical_size()
#   esp_timer          - esp_timer_get_time() for uptime
# The core components (freertos, log, esp_hw_support, heap) are always
# available, so esp_chip_info.h / esp_mac.h / esp_heap_caps.h need no entry.
idf_component_register(SRCS "secure_boot_demo_main.c"
                       PRIV_REQUIRES bootloader_support
                                     efuse
                                     esp_app_format
                                     app_update
                                     esp_partition
                                     spi_flash
                                     esp_timer
                       INCLUDE_DIRS ".")
