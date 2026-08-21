/*
 * Secure Boot v2 — Learning Demo for ESP32-C5
 * ---------------------------------------------
 * This app does NOT enable Secure Boot by itself. Enabling Secure Boot burns
 * eFuses and is IRREVERSIBLE — that is driven entirely by menuconfig + the
 * bootloader, never by this file (see README.md).
 *
 * What this app DOES: at boot it prints a numbered STEP report — first the
 * device's own identity (chip, MAC, flash, firmware image, running partition),
 * then the security state read straight out of the eFuses. Flash the SAME
 * unchanged app in three configurations and watch the report change:
 *
 *   STEP 1  Plain build (no signing)             -> Secure Boot: DISABLED
 *   STEP 2  Signed-app verification (software)   -> Secure Boot: DISABLED  (but app is signed)
 *   STEP 3  Hardware Secure Boot v2 enabled      -> Secure Boot: ENABLED   (eFuse burned)
 *
 * The STEP number is derived at COMPILE time from Kconfig, because steps 1 and
 * 2 are indistinguishable at runtime (software signing burns nothing). The
 * security lines below it are read at RUNTIME from the real eFuse bits. Showing
 * both side by side is the whole lesson: signing an app is not the same thing
 * as locking the hardware.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

/* These two headers expose the runtime "is the feature really on?" queries.
 * They read the actual eFuse bits over the eFuse controller — they do NOT
 * trust a compile-time #define, so the answer is the ground truth. */
#include "esp_secure_boot.h"     /* esp_secure_boot_enabled()                    */
#include "esp_flash_encrypt.h"   /* esp_get_flash_encryption_mode()              */
#include "esp_efuse.h"           /* esp_efuse_is_flash_encryption_enabled(), ... */
#include "esp_efuse_table.h"     /* ESP_EFUSE_* field descriptors for this target */

static const char *TAG = "SB_DEMO";

/* ---------------------------------------------------------------------------
 * Which lab STEP is this build?  Decided at compile time, not at runtime.
 *
 * Runtime eFuse queries CANNOT tell step 1 from step 2: software signing
 * (CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT) verifies signatures in the app
 * layer and burns nothing, so esp_secure_boot_enabled() stays false in both.
 * The only honest source for "which stage did I build" is Kconfig itself, in
 * the same precedence order the docs use when grepping sdkconfig.
 *
 * Note the "#if defined()" form: boolean Kconfig symbols are either defined as
 * 1 or absent entirely, so "#if CONFIG_X == 1" would break on the absent case.
 * ------------------------------------------------------------------------- */
#if defined(CONFIG_SECURE_BOOT)
#  define DEMO_STEP      3
#  define DEMO_STEP_NAME "HARDWARE SECURE BOOT v2  (eFuses burned - IRREVERSIBLE)"
#  define DEMO_BUILD_ST  "C - signed bootloader + signed app, hardware enforced"
#elif defined(CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT)
#  define DEMO_STEP      2
#  define DEMO_STEP_NAME "SIGNED APP  (software verification, no eFuse burned)"
#  define DEMO_BUILD_ST  "B - app is signed, hardware still unlocked (reversible)"
#else
#  define DEMO_STEP      1
#  define DEMO_STEP_NAME "DEVICE INFO / BASELINE  (plain, unsigned build)"
#  define DEMO_BUILD_ST  "A - nothing signed, nothing burned (fully reversible)"
#endif

/* Which console is printf() going to?  Useful when the board enumerates as two
 * different COM ports (USB-UART bridge vs native USB-Serial-JTAG). */
#if defined(CONFIG_ESP_CONSOLE_UART) || defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
#  define DEMO_CONSOLE "UART (see CONFIG_ESP_CONSOLE_UART_NUM)"
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#  define DEMO_CONSOLE "USB-Serial-JTAG"
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
#  define DEMO_CONSOLE "USB-CDC"
#else
#  define DEMO_CONSOLE "none"
#endif

#define SEP_THICK "=================================================="
#define SEP_THIN  "--------------------------------------------------"

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

/* esp_chip_info() reports a model enum; turn it into the marketing name so the
 * report proves WHICH silicon answered, not just that something answered. */
static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
        case CHIP_ESP32:    return "ESP32";
        case CHIP_ESP32S2:  return "ESP32-S2";
        case CHIP_ESP32S3:  return "ESP32-S3";
        case CHIP_ESP32C3:  return "ESP32-C3";
        case CHIP_ESP32C2:  return "ESP32-C2";
        case CHIP_ESP32C5:  return "ESP32-C5";
        case CHIP_ESP32C6:  return "ESP32-C6";
        case CHIP_ESP32C61: return "ESP32-C61";
        case CHIP_ESP32H2:  return "ESP32-H2";
        case CHIP_ESP32P4:  return "ESP32-P4";
        default:            return "UNKNOWN";
    }
}

/* Why the reset happened. On a Secure Boot board a signature failure shows up
 * here as a reboot loop, so this line is diagnostic, not decoration. */
static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON (cold boot)";
        case ESP_RST_EXT:       return "EXT (reset pin)";
        case ESP_RST_SW:        return "SW (esp_restart)";
        case ESP_RST_PANIC:     return "PANIC (exception/abort)";
        case ESP_RST_INT_WDT:   return "INT_WDT (interrupt watchdog)";
        case ESP_RST_TASK_WDT:  return "TASK_WDT (task watchdog)";
        case ESP_RST_WDT:       return "WDT (other watchdog)";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP wake";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (supply dipped)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

/* MAC addresses live in eFuse BLK1 and are readable even with Secure Boot on —
 * they identify the board, they are not a secret. */
static void print_mac(const char *label, esp_mac_type_t type, size_t len)
{
    uint8_t mac[8] = {0};
    if (esp_read_mac(mac, type) != ESP_OK) {
        printf("  %-16s: <unavailable>\n", label);
        return;
    }
    printf("  %-16s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X%s", mac[i], (i + 1 < len) ? ":" : "\n");
    }
}

/* -------- STEP block 1: what silicon am I actually running on? -------- */
static void report_device_identity(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("[1/5] DEVICE IDENTITY\n");
    printf("  %-16s: %s\n",    "Chip model",  chip_model_str(chip.model));
    printf("  %-16s: v%d.%d\n", "Silicon rev", chip.revision / 100, chip.revision % 100);
    printf("  %-16s: %d\n",    "CPU cores",   chip.cores);

    /* Feature bits are fused in at the factory — this is the chip telling us
     * which radios it has, not what the SDK was compiled for. */
    printf("  %-16s:", "Radios/feat");
    if (chip.features & CHIP_FEATURE_WIFI_BGN)   printf(" WiFi-2.4G");
    if (chip.features & CHIP_FEATURE_BLE)        printf(" BLE");
    if (chip.features & CHIP_FEATURE_BT)         printf(" BT-Classic");
    if (chip.features & CHIP_FEATURE_IEEE802154) printf(" 802.15.4");
    if (chip.features & CHIP_FEATURE_EMB_FLASH)  printf(" embedded-flash");
    if (chip.features & CHIP_FEATURE_EMB_PSRAM)  printf(" embedded-PSRAM");
    printf("  (raw 0x%08" PRIX32 ")\n", chip.features);
    if (chip.model == CHIP_ESP32C5) {
        /* Don't let the bitmask mislead: the C5 radio is dual-band, but IDF has
         * no 5 GHz feature bit — CHIP_FEATURE_WIFI_BGN only covers 2.4 GHz. The
         * C5 port also never sets EMB_FLASH/EMB_PSRAM/BT, so their absence here
         * says nothing about the module. */
        printf("  %-16s: radio is dual-band 2.4+5 GHz; IDF has no 5 GHz feature bit\n", "  note");
    }

    /* Two different numbers on purpose: the size the build was configured for
     * vs. the size the chip really reports. A mismatch silently truncates the
     * flash layout, so it is worth seeing both before anything gets locked. */
    uint32_t cfg_size = 0, phy_size = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &cfg_size) == ESP_OK) {
        printf("  %-16s: %" PRIu32 " KB (configured, CONFIG_ESPTOOLPY_FLASHSIZE=%s)\n",
               "Flash size", cfg_size / 1024, CONFIG_ESPTOOLPY_FLASHSIZE);
    }
    if (esp_flash_get_physical_size(esp_flash_default_chip, &phy_size) == ESP_OK) {
        printf("  %-16s: %" PRIu32 " KB (physical, as reported by the chip)\n",
               "Flash detected", phy_size / 1024);
    }

    print_mac("Base MAC", ESP_MAC_BASE, 6);
    print_mac("WiFi STA MAC", ESP_MAC_WIFI_STA, 6);
    printf("  %-16s: %s\n", "Console", DEMO_CONSOLE);
}

/* -------- STEP block 2: which image is this, and where does it live? -------- */
static void report_firmware_image(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    printf("%s\n", SEP_THIN);
    printf("[2/5] FIRMWARE IMAGE\n");
    printf("  %-16s: %s\n",    "Project",       app ? app->project_name : "?");
    printf("  %-16s: %s\n",    "App version",   app ? app->version      : "?");
    printf("  %-16s: %s %s\n", "Compiled",      app ? app->date : "?", app ? app->time : "");
    printf("  %-16s: %s\n",    "ESP-IDF",       esp_get_idf_version());
    printf("  %-16s: %" PRIu32 "  (anti-rollback counter in the image header)\n",
           "Secure version", app ? app->secure_version : 0);

    /* The ELF SHA-256 is the cheapest way to prove "the binary on the board is
     * the binary I just built" — compare it with the build output. */
    char sha[17] = {0};
    if (esp_app_get_elf_sha256(sha, sizeof(sha)) > 0) {
        printf("  %-16s: %s...\n", "ELF SHA256", sha);
    }

    /* Which partition the bootloader actually chose to run. This project's
     * partitions.csv declares factory only (no OTA slots), so it must say
     * 'factory' — if it ever doesn't, the layout changed under you. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        printf("  %-16s: %s  (type %d, subtype %d)\n",
               "Running part", run->label, (int)run->type, (int)run->subtype);
        printf("  %-16s: offset 0x%06" PRIX32 ", size %" PRIu32 " KB\n",
               "Part location", (uint32_t)run->address, (uint32_t)run->size / 1024);
    }
    printf("  %-16s: 0x%X  (pushed out to leave room for a signed bootloader)\n",
           "Part table @", CONFIG_PARTITION_TABLE_OFFSET);
}

/* -------- STEP block 3: is the board healthy right now? -------- */
static void report_runtime_health(void)
{
    printf("%s\n", SEP_THIN);
    printf("[3/5] RUNTIME HEALTH\n");
    printf("  %-16s: %s\n", "Reset reason", reset_reason_str(esp_reset_reason()));
    printf("  %-16s: %" PRIu32 " bytes\n", "Free heap",     esp_get_free_heap_size());
    printf("  %-16s: %" PRIu32 " bytes\n", "Min free heap", esp_get_minimum_free_heap_size());
    printf("  %-16s: %u bytes\n", "Largest block",
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    printf("  %-16s: %" PRId64 " ms since boot\n", "Uptime", esp_timer_get_time() / 1000);
}

/* The C5 has six key slots (EFUSE_BLK_KEY0..KEY5). Secure Boot v2 consumes one
 * per public-key digest, up to three, which is what makes key rotation and
 * revocation possible. Note this is the ESP32-C5 list specifically: the type is
 * esp_efuse_purpose_t (there is no esp_efuse_key_purpose_t), and C5 has no
 * XTS_AES_256 purposes because its flash encryption is XTS-AES-128. */
static const char *key_purpose_str(esp_efuse_purpose_t p)
{
    switch (p) {
        case ESP_EFUSE_KEY_PURPOSE_USER:                        return "USER (free for software)";
        case ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY:                   return "ECDSA_KEY (P256)";
        case ESP_EFUSE_KEY_PURPOSE_XTS_AES_128_KEY:             return "XTS_AES_128_KEY (flash encryption)";
        case ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_ALL:               return "HMAC_DOWN_ALL";
        case ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_JTAG:              return "HMAC_DOWN_JTAG";
        case ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE: return "HMAC_DOWN_DS";
        case ESP_EFUSE_KEY_PURPOSE_HMAC_UP:                     return "HMAC_UP";
        case ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST0:         return "SECURE_BOOT_DIGEST0";
        case ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST1:         return "SECURE_BOOT_DIGEST1";
        case ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST2:         return "SECURE_BOOT_DIGEST2";
        case ESP_EFUSE_KEY_PURPOSE_KM_INIT_KEY:                 return "KM_INIT_KEY";
        case ESP_EFUSE_KEY_PURPOSE_XTS_AES_128_PSRAM_KEY:       return "XTS_AES_128_PSRAM_KEY";
        case ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY_P192:              return "ECDSA_KEY_P192 (weak legacy curve)";
        case ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY_P384_L:            return "ECDSA_KEY_P384_L";
        case ESP_EFUSE_KEY_PURPOSE_ECDSA_KEY_P384_H:            return "ECDSA_KEY_P384_H";
        default:                                                return "UNKNOWN/unreadable";
    }
}

/* -------- STEP block 5: the fuses themselves --------
 * EVERY call in here is read-only. The esp_efuse_set_* / esp_efuse_write_*
 * family permanently blows fuses and must never appear in a status reporter.
 *
 * In STEP 1 this whole block should read as "nothing burned": no digests, no
 * revocations, all six key slots free. That empty state IS the baseline — it is
 * what STEP 3 fills in, one-way. */
static void report_efuse_state(void)
{
    printf("%s\n", SEP_THIN);
    printf("[5/5] eFUSE DETAIL (read-only queries, nothing is written here)\n");

    /* The raw bit behind esp_secure_boot_enabled(), shown side by side so it is
     * obvious the API is not inventing its answer. */
    printf("  %-16s: %d\n", "SECURE_BOOT_EN", (int)esp_efuse_read_field_bit(ESP_EFUSE_SECURE_BOOT_EN));

    /* Secure Boot v2 supports 3 public-key digests; revoking one retires that
     * key after an OTA signed by a newer one. All zero until you rotate. */
    printf("  %-16s: %d %d %d  (digest 0/1/2; 1 = that key is retired)\n", "Key revoke",
           (int)esp_efuse_read_field_bit(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE0),
           (int)esp_efuse_read_field_bit(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE1),
           (int)esp_efuse_read_field_bit(ESP_EFUSE_SECURE_BOOT_KEY_REVOKE2));

    /* SPI_BOOT_CRYPT_CNT is a 3-bit counter, not a flag: an ODD number of set
     * bits means flash encryption is on. That is how one fuse field encodes
     * enable -> disable -> enable while only ever going one way. */
    size_t crypt_cnt = 0;
    if (esp_efuse_read_field_cnt(ESP_EFUSE_SPI_BOOT_CRYPT_CNT, &crypt_cnt) == ESP_OK) {
        printf("  %-16s: %u bit(s) set -> %s\n", "SPI_BOOT_CRYPT", (unsigned)crypt_cnt,
               (crypt_cnt % 2) ? "flash encryption ON" : "flash encryption OFF");
    }

    /* Anti-rollback: the bootloader refuses images whose secure_version is
     * lower than this. Stays 0 until you deliberately raise it. */
    printf("  %-16s: %" PRIu32 "  (anti-rollback floor burned in eFuse)\n",
           "SECURE_VERSION", esp_efuse_read_secure_version());

    /* The doors an attacker would use to read or replace flash. */
    printf("  %-16s: dl_mode_dis=%d secure_dl=%d pad_jtag_dis=%d usb_jtag_dis=%d\n", "Debug/DL doors",
           (int)esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE),
           (int)esp_efuse_read_field_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD),
           (int)esp_efuse_read_field_bit(ESP_EFUSE_DIS_PAD_JTAG),
           (int)esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_JTAG));

    /* Write/read protection masks. Non-zero here means some fuses are already
     * sealed shut. NOTE the third argument is a BIT count, not sizeof(). */
    uint32_t wr_dis = 0;
    uint8_t  rd_dis = 0;
    esp_efuse_read_field_blob(ESP_EFUSE_WR_DIS, &wr_dis, 32);
    esp_efuse_read_field_blob(ESP_EFUSE_RD_DIS, &rd_dis, 7);
    printf("  %-16s: WR_DIS=0x%08" PRIX32 "  RD_DIS=0x%02X\n", "Protect masks", wr_dis, rd_dis);

    /* Per-slot inventory. "free" here is the interesting column in STEP 1:
     * every slot free means no root of trust has been installed yet. */
    int free_slots = 0;
    for (esp_efuse_block_t b = EFUSE_BLK_KEY0; b < EFUSE_BLK_KEY_MAX; b++) {
        bool unused = esp_efuse_key_block_unused(b);
        if (unused) {
            free_slots++;
            printf("  KEY%d slot       : FREE\n", (int)(b - EFUSE_BLK_KEY0));
        } else {
            printf("  KEY%d slot       : USED  purpose=%s  rd_prot=%d wr_prot=%d\n",
                   (int)(b - EFUSE_BLK_KEY0), key_purpose_str(esp_efuse_get_key_purpose(b)),
                   (int)esp_efuse_get_key_dis_read(b), (int)esp_efuse_get_key_dis_write(b));
        }
    }
    printf("  %-16s: %d of %d\n", "Free key slots", free_slots,
           (int)(EFUSE_BLK_KEY_MAX - EFUSE_BLK_KEY0));

    /* Optional 128-bit factory ID. Espressif does not program it on every part,
     * so all-zero means "absent", not "zero". MAC is the reliable device ID. */
    uint8_t uid[16] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, 128) == ESP_OK) {
        bool any = false;
        for (size_t i = 0; i < sizeof(uid); i++) {
            if (uid[i]) { any = true; break; }
        }
        printf("  %-16s: ", "Chip UID");
        if (!any) {
            printf("<not programmed on this part>\n");
        } else {
            for (size_t i = 0; i < sizeof(uid); i++) {
                printf("%02X", uid[i]);
            }
            printf("\n");
        }
    }
}

void app_main(void)
{
    /* -------- STEP banner: compile-time truth -------- */
    printf("\n");
    printf("%s\n", SEP_THICK);
    printf("  ESP32-C5 Secure Boot v2 — Runtime Status Report\n");
    printf("  STEP %d of 3 : %s\n", DEMO_STEP, DEMO_STEP_NAME);
    printf("  Build state  : %s\n", DEMO_BUILD_ST);
    printf("%s\n", SEP_THICK);

    report_device_identity();
    report_firmware_image();
    report_runtime_health();

    /* -------- STEP block 4: runtime truth, straight from the eFuses --------
     * esp_secure_boot_enabled() returns true only when the SECURE_BOOT_EN eFuse
     * has been burned. Once true, the ROM will refuse to run any bootloader
     * whose signature does not match a public-key digest stored in eFuse. It
     * can never go back to false — the eFuse is a one-way fuse.
     *
     * Compare this block against the STEP banner above: in STEP 2 the banner
     * says the app is signed while this block still reads DISABLED. That gap is
     * the point of the whole lab. */
    bool secure_boot_on = esp_secure_boot_enabled();
    /* esp_efuse_is_flash_encryption_enabled() is the non-deprecated spelling as
     * of ESP-IDF v6.0; esp_flash_encryption_enabled() still forwards to it but
     * now emits -Wdeprecated-declarations. */
    bool flash_enc_on   = esp_efuse_is_flash_encryption_enabled();
    esp_flash_enc_mode_t fe_mode = esp_get_flash_encryption_mode();

    printf("%s\n", SEP_THIN);
    printf("[4/5] SECURITY STATE (read from eFuse, not from config)\n");
    printf("  Secure Boot v2  : %s\n", secure_boot_on ? "ENABLED  (eFuse burned, irreversible)"
                                                      : "DISABLED (device will run unsigned code)");
    printf("  Flash Encrypt.  : %s\n", flash_enc_on ? "ENABLED" : "DISABLED");
    printf("  Flash Enc. mode : %s\n", flash_enc_mode_str(fe_mode));

    report_efuse_state();
    printf("%s\n", SEP_THICK);

    /* -------- Explain what the numbers mean for THIS boot -------- */
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

    /* Tell the operator what this STEP proved and what the next one is. */
#if DEMO_STEP == 1
    ESP_LOGI(TAG, "STEP 1 done: the device answered and identified itself. Nothing signed, nothing burned.");
    ESP_LOGI(TAG, "Next: STEP 2 = sign the app in software (reversible). See README.md 'Stage 2'.");
#elif DEMO_STEP == 2
    ESP_LOGI(TAG, "STEP 2 done: this app is SIGNED, yet Secure Boot above still reads DISABLED.");
    ESP_LOGI(TAG, "Next: STEP 3 = burn the eFuse (IRREVERSIBLE, spare boards only). README.md 'Stage 3'.");
#else
    ESP_LOGI(TAG, "STEP 3 done: hardware root of trust is active. This board is locked permanently.");
#endif

    /* Heartbeat so you can confirm the app is alive on the monitor. */
    uint32_t n = 0;
    while (true) {
        ESP_LOGI(TAG, "alive: secure_boot=%d flash_enc=%d  (tick %" PRIu32 ")",
                 (int)secure_boot_on, (int)flash_enc_on, n++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
