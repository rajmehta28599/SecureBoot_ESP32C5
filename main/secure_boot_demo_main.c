/*
 * Secure Boot v2 — Learning Demo for ESP32-C5
 * ---------------------------------------------
 * This app does NOT enable Secure Boot by itself. Enabling Secure Boot burns
 * eFuses and is IRREVERSIBLE — that is driven entirely by menuconfig + the
 * bootloader, never by this file (see README.md).
 *
 * What this app DOES: at runtime it reads the chip's eFuses through the public
 * ESP-IDF API and prints, in plain language, whether the security features are
 * currently ON or OFF. Flash it in three states and watch the output change:
 *
 *   State A  Plain build (no signing)             -> Secure Boot: DISABLED
 *   State B  Signed-app verification (software)   -> Secure Boot: DISABLED  (but app is signed)
 *   State C  Hardware Secure Boot v2 enabled      -> Secure Boot: ENABLED   (eFuse burned)
 *
 * That is the whole point of the demo: SEE the transition from an unprotected
 * device to a locked one, and prove to yourself which switch actually flipped
 * the hardware bit.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"

/* These two headers expose the runtime "is the feature really on?" queries.
 * They read the actual eFuse bits over the eFuse controller — they do NOT
 * trust a compile-time #define, so the answer is the ground truth. */
#include "esp_secure_boot.h"     /* esp_secure_boot_enabled()            */
#include "esp_flash_encrypt.h"   /* esp_flash_encryption_enabled(), mode */

static const char *TAG = "SB_DEMO";

/* Pretty-print the flash-encryption mode enum returned by the ROM. */
static const char *flash_enc_mode_str(esp_flash_enc_mode_t mode)
{
    switch (mode) {
        case ESP_FLASH_ENC_MODE_DISABLED:    return "DISABLED";
        case ESP_FLASH_ENC_MODE_DEVELOPMENT: return "DEVELOPMENT (re-flashable, NOT for production)";
        case ESP_FLASH_ENC_MODE_RELEASE:     return "RELEASE (locked down)";
        default:                             return "UNKNOWN";
    }
}

void app_main(void)
{
    /* -------- 1. Who am I running on? -------- */
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n");
    printf("==================================================\n");
    printf("  ESP32-C5 Secure Boot v2 — Runtime Status Report\n");
    printf("==================================================\n");
    printf("  ESP-IDF version : %s\n", esp_get_idf_version());
    printf("  Chip cores      : %d\n", chip.cores);
    printf("  Silicon rev     : v%d.%d\n", chip.revision / 100, chip.revision % 100);

    /* -------- 2. Is HARDWARE Secure Boot really enabled? --------
     * This returns true only when the SECURE_BOOT_EN eFuse has been burned.
     * Once true, the ROM will refuse to run any bootloader whose signature
     * does not match a public-key digest stored in eFuse. It can never go
     * back to false — the eFuse is a one-way fuse. */
    bool secure_boot_on = esp_secure_boot_enabled();
    printf("--------------------------------------------------\n");
    printf("  Secure Boot v2  : %s\n", secure_boot_on ? "ENABLED  (eFuse burned, irreversible)"
                                                       : "DISABLED (device will run unsigned code)");

    /* -------- 3. Is Flash Encryption enabled? --------
     * Secure Boot proves *authenticity* (only your code runs). Flash
     * Encryption proves *confidentiality* (the code/keys can't be read out).
     * They are independent switches; production devices want BOTH. */
    bool flash_enc_on = esp_flash_encryption_enabled();
    esp_flash_enc_mode_t fe_mode = esp_get_flash_encryption_mode();
    printf("  Flash Encrypt.  : %s\n", flash_enc_on ? "ENABLED" : "DISABLED");
    printf("  Flash Enc. mode : %s\n", flash_enc_mode_str(fe_mode));
    printf("--------------------------------------------------\n");

    /* -------- 4. Explain what the numbers mean for THIS boot -------- */
    if (secure_boot_on) {
        ESP_LOGI(TAG, "This bootloader + app were signature-verified by hardware before running.");
        ESP_LOGI(TAG, "Flashing an unsigned or differently-signed image will now brick the boot.");
    } else {
        ESP_LOGW(TAG, "Secure Boot is OFF: any image can be flashed and will run. Fine for learning,");
        ESP_LOGW(TAG, "unsafe for production. See README.md 'Stage 3' to burn the eFuse on a spare board.");
    }

    if (!flash_enc_on) {
        ESP_LOGW(TAG, "Flash is stored in plaintext — readable with 'esptool read_flash'.");
    }

    /* Heartbeat so you can confirm the app is alive on the monitor. */
    uint32_t n = 0;
    while (true) {
        ESP_LOGI(TAG, "alive: secure_boot=%d flash_enc=%d  (tick %" PRIu32 ")",
                 (int)secure_boot_on, (int)flash_enc_on, n++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
