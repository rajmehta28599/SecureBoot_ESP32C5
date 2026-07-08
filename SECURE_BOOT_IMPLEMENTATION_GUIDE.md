# Secure Boot Implementation Guide

This guide shows the practical implementation path for this ESP32-C5 Secure Boot demo. It covers normal code changes, signed builds, hardware Secure Boot provisioning, USB/UART updates, OTA design, and key rotation.

Use a spare board for hardware Secure Boot. Stage 3 burns eFuses and cannot be undone.

## 1. Target End State

Production-style flow:

```text
Developer changes code
        |
        v
ESP-IDF builds firmware
        |
        v
Build system signs app with private key
        |
        v
Device receives firmware by USB/UART or OTA
        |
        v
ROM verifies bootloader
        |
        v
Bootloader verifies app
        |
        v
Only trusted signed firmware runs
```

What the device trusts:

```text
PC or HSM:
  private signing key, secret, never flashed

ESP32-C5 eFuse:
  public-key digest, permanent

Firmware image:
  app bytes + signature block + public key
```

## 2. Current Project Layout

Current files:

| File | Purpose |
|---|---|
| `main/secure_boot_demo_main.c` | Runtime app that prints chip and security status |
| `main/CMakeLists.txt` | Registers app source and required ESP-IDF components |
| `sdkconfig.defaults` | Safe Stage 1 defaults plus commented Stage 2 and Stage 3 references |
| `partitions.csv` | Factory-only partition table with extra bootloader room |

Current partition layout:

```text
nvs
phy_init
factory app
```

This means the current repo supports USB/UART lab flashing. OTA is not active yet.

## 3. Step 1: Build The Safe Baseline

Start from the ESP-IDF PowerShell terminal in this folder.

```powershell
idf.py set-target esp32c5
idf.py build
idf.py -p COM15 flash monitor
```

Expected result:

```text
Secure Boot v2  : DISABLED
Flash Encrypt.  : DISABLED
alive: secure_boot=0 flash_enc=0
```

Visual:

```text
unsigned build -> USB/UART flash -> chip boots anything compatible
```

This stage is reversible and burns no eFuses.

## 4. Step 2: Make A Code Change And Reupload

Edit only app code under `main/`. For example, change the report title in `main/secure_boot_demo_main.c`:

```c
printf("  ESP32-C5 Secure Boot v2 - Runtime Status Report\n");
```

Then rebuild and reupload:

```powershell
idf.py build
idf.py -p COM15 flash monitor
```

Before Secure Boot is enabled, any compatible app can boot. After Secure Boot is enabled, the changed app must be signed by the trusted key.

```text
code changed + unsigned + Secure Boot off = boots
code changed + signed with trusted key + Secure Boot on = boots
code changed + unsigned + Secure Boot on = rejected
```

## 5. Step 3: Generate The Signing Key

Generate one ECDSA-256 key for this demo:

```powershell
idf.py secure-generate-signing-key --scheme ecdsa256 secure_boot_signing_key.pem
```

Key rules:

| Rule | Reason |
|---|---|
| Do not commit `*.pem` | Anyone with the key can sign trusted firmware |
| Back it up offline | Losing it means locked boards cannot be updated |
| Keep it off devices | Devices need only the public-key digest |
| Prefer HSM for production | Reduces private-key exposure |

## 6. Step 4: Rehearse Signing Without Hardware Lock

This is Stage 2. It signs the app but does not burn eFuses.

Use `idf.py menuconfig`:

```text
Security features
  Require signed app images
  App Signing Scheme: ECDSA v2
  ECDSA key length: 256
  Signing key: secure_boot_signing_key.pem
```

Or enable the Stage 2 block in `sdkconfig.defaults`, then run:

```powershell
idf.py fullclean
idf.py build
idf.py -p COM15 flash monitor
espsecure signature-info-v2 build/secure_boot_demo.bin
```

Visual:

```text
source code
   |
   v
idf.py build
   |
   v
signed app image
   |
   v
device still reports hardware Secure Boot disabled
```

This stage proves the signing workflow while recovery is still easy.

## 7. Step 5: Enable Hardware Secure Boot

Do this only on a spare board. This is Stage 3 and is irreversible.

In `idf.py menuconfig`:

```text
Security features
  Enable hardware Secure Boot in bootloader
  Secure Boot v2
  App Signing Scheme: ECDSA v2
  ECDSA key length: 256
  Secure Boot signing key: secure_boot_signing_key.pem
  Optional: enable Flash Encryption in Development mode for lab testing
```

Build the signed bootloader:

```powershell
idf.py bootloader
```

ESP-IDF prints the exact `esptool write-flash ... bootloader.bin` command. Run only that printed command for your board and port.

Then flash the signed partition table and app:

```powershell
idf.py -p COM15 flash monitor
```

First secure boot flow:

```text
signed bootloader flashed
        |
        v
signed app flashed
        |
        v
first boot starts
        |
        v
bootloader burns Secure Boot eFuses
        |
        v
device resets or continues boot
        |
        v
ROM now verifies bootloader forever
```

Verify:

```powershell
idf.py -p COM15 efuse-summary
```

Look for:

```text
SECURE_BOOT_EN = 1
one BLOCK_KEYx contains Secure Boot public-key digest
```

## 8. Step 6: Updating Firmware After Secure Boot

After Secure Boot is enabled, normal code updates still work if they are signed.

```text
change app code
   |
   v
idf.py build signs app
   |
   v
idf.py -p COM15 flash monitor
   |
   v
bootloader verifies signature
   |
   v
app boots
```

Decision table:

| Uploaded image | Result |
|---|---|
| Signed with trusted key | Boots |
| Unsigned | Rejected |
| Signed with wrong key | Rejected |
| Modified after signing | Rejected |
| Old signed image | Boots unless anti-rollback blocks it |

Avoid reflashing the bootloader during normal development after Secure Boot is enabled.

## 9. Step 7: OTA Implementation Plan

The current `partitions.csv` is factory-only. To implement OTA, change to an OTA partition layout before locking production devices.

Example OTA partition table:

```csv
# Name,     Type, SubType,  Offset,  Size,   Flags
nvs,        data, nvs,      ,        0x6000,
otadata,    data, ota,      ,        0x2000,
phy_init,   data, phy,      ,        0x1000,
ota_0,      app,  ota_0,    ,        1M,
ota_1,      app,  ota_1,    ,        1M,
```

OTA flow:

```text
running app
   |
   v
connect Wi-Fi or Ethernet
   |
   v
download signed firmware from HTTPS server
   |
   v
write to inactive OTA slot
   |
   v
mark slot bootable
   |
   v
reboot
   |
   v
bootloader verifies new signed app
   |
   +-- valid   -> boot new app
   |
   +-- invalid -> reject or roll back
```

Typical OTA component registration:

```cmake
idf_component_register(SRCS "secure_boot_demo_main.c"
                       PRIV_REQUIRES bootloader_support esp_https_ota esp_http_client app_update
                       INCLUDE_DIRS ".")
```

Minimal OTA function shape:

```c
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"

static void run_ota_update(const char *url, const char *server_cert_pem)
{
    esp_http_client_config_t http_config = {
        .url = url,
        .cert_pem = server_cert_pem,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t err = esp_https_ota(&ota_config);
    if (err == ESP_OK) {
        esp_restart();
    }

    ESP_LOGE("OTA", "OTA failed: %s", esp_err_to_name(err));
}
```

Production OTA must also include Wi-Fi provisioning, TLS certificate management, rollback confirmation, version checks, server authentication, and staged rollout handling.

Rollback confirmation shape:

```c
#include "esp_ota_ops.h"
#include "esp_system.h"

static bool app_self_test_passed(void)
{
    /* Replace with real checks: NVS init, network, sensors, secure config, etc. */
    return true;
}

static void confirm_or_rollback_ota(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    if (app_self_test_passed()) {
        esp_ota_mark_app_valid_cancel_rollback();
        return;
    }

    esp_ota_mark_app_invalid_rollback_and_reboot();
}
```

Call `confirm_or_rollback_ota()` early in `app_main()` after essential services are initialized. This prevents a broken OTA image from becoming permanent.

## 10. Step 8: Key Rotation Implementation

The ESP32-C5 supports up to three Secure Boot public-key digest slots. Key rotation works only if the future keys were provisioned before locking.

Good factory plan:

```text
slot 0: key_0 digest active
slot 1: key_1 digest active
slot 2: key_2 digest active
```

Rotation flow:

```text
current app signed with key_0
        |
        v
OTA app signed with key_1
        |
        v
device verifies key_1 because slot 1 is trusted
        |
        v
new app boots successfully
        |
        v
new app revokes key_0
        |
        v
key_0 can never be used again
```

Revocation call shape:

```c
#include "esp_ota_ops.h"

void revoke_old_secure_boot_key(void)
{
    esp_ota_revoke_secure_boot_public_key(SECURE_BOOT_PUBLIC_KEY_INDEX_0);
}
```

Only revoke after the new signed app is proven. Revoking the only working key bricks future updates.

## 11. Step 9: Flash Encryption Considerations

Secure Boot proves authenticity. Flash Encryption protects confidentiality.

| Feature | Stops | Does not stop |
|---|---|---|
| Secure Boot | Unsigned or modified code running | Plaintext flash readout |
| Flash Encryption | Reading flash contents | Trust decisions by itself |
| Both | Fake firmware and flash readout | Poor key handling |

For lab testing, use Flash Encryption Development mode. For production, use Release mode only after the update and recovery process is proven.

## 12. Testing Scenarios: Success And Failure

Use this matrix to understand what should pass, what should fail, and what each result proves. Do not test bad bootloaders on a locked board.

| Test | Device state | Action | Expected result | What it proves |
|---|---|---|---|---|
| Baseline boot | Secure Boot off | `idf.py build`, then `idf.py -p COM15 flash monitor` | App boots and prints Secure Boot disabled | Board, port, toolchain, and app are working |
| Normal code change | Secure Boot off | Change a print line, rebuild, flash | Modified app boots | Plain development flow works |
| Signed app rehearsal | Stage 2 only | Generate key, enable signed app config, build | Signature block exists, hardware bit still disabled | Signing flow works without eFuse risk |
| Wrong or unsigned app | Secure Boot off | Flash another compatible app | App boots | This is the insecurity Secure Boot fixes |
| Valid signed update | Secure Boot on | Build app with trusted key, flash app | App boots | Signed updates still work |
| Unsigned app after lock | Secure Boot on, spare board only | Flash an unsigned app, not bootloader | Bootloader rejects app | App authenticity enforcement works |
| Tampered signed app | Secure Boot on, spare board only | Modify image after signing | Boot fails | Integrity check works |
| Wrong signing key | Secure Boot on, spare board only | Sign app with untrusted key | Boot fails | eFuse key digest check works |
| Old signed app | Secure Boot on | Flash older signed app | Boots unless anti-rollback is enabled | Signature alone does not block rollback |
| OTA bad image | OTA-enabled build | Download corrupted or wrong-key image | OTA rejects or rolls back | OTA recovery path works |
| Flash readout | Secure Boot only | Read flash externally | Plaintext may be visible | Secure Boot is not confidentiality |
| Flash readout | Secure Boot plus Flash Encryption | Read flash externally | Data is encrypted | Flash Encryption protects contents |

Visual pass/fail flow:

```text
new firmware received
        |
        v
signature block present?
        |
        +-- no -> reject after Secure Boot
        |
        v
public-key digest trusted in eFuse?
        |
        +-- no -> reject
        |
        v
image hash still matches?
        |
        +-- no -> reject
        |
        v
anti-rollback version acceptable?
        |
        +-- no -> reject
        |
        v
boot firmware
```

Safe learning order:

```text
1. Test success in Stage 1
2. Test code-change reupload in Stage 1
3. Test signed image creation in Stage 2
4. Inspect eFuse summary before locking
5. Lock only a spare board
6. Test valid signed update
7. Test invalid app rejection only if recovery path is proven
8. Never test invalid bootloader on a locked board
```

## 13. Security Gaps, Loopholes, And Options

Secure Boot is strong, but it is not a complete product security plan by itself.

| Gap or loophole | Why it matters | Mitigation |
|---|---|---|
| Flash remains readable | Secure Boot authenticates code but does not hide it | Enable Flash Encryption |
| Old signed firmware can boot | A vulnerable old version may still be validly signed | Enable anti-rollback and manage secure version carefully |
| Private key leak | An attacker with the key can sign trusted firmware | Use offline storage or HSM, access control, and key rotation slots |
| Single-key setup | Lost or leaked key cannot be replaced | Provision multiple Secure Boot key slots before production lock |
| Insecure OTA transport | Device may receive attacker-controlled updates | Use HTTPS, certificate validation, version checks, and signed images |
| Bad trusted firmware | Secure Boot runs anything signed by your key, even buggy code | Add release review, testing, staged rollout, and rollback |
| Debug/download access | Physical access may allow probing or recovery paths | Lock JTAG and choose secure UART download policy |
| No OTA partition | Failed field update has no alternate slot | Add `otadata`, `ota_0`, and `ota_1` before production |
| Flash Encryption release mode | Recovery over UART can become difficult or impossible | Prove OTA and factory process before release lock |
| Aggressive key revocation | Repeated failures can revoke all keys and brick the board | Use conservative revocation unless threat model requires aggressive mode |
| ECDSA-P192 | Weak legacy security level | Use ECDSA-P256 by default or P-384 if required |

Common implementation options:

| Option | Best for | Trade-off |
|---|---|---|
| Stage 1 plain firmware | Early development | No security |
| Stage 2 signed app only | Rehearsing signing safely | Hardware still runs replaceable bootloader |
| Secure Boot only | Preventing unsigned firmware execution | Flash can still be read |
| Secure Boot plus Flash Encryption | Production device protection | More complex update and recovery flow |
| USB/UART update | Lab and factory | Not suitable for remote fleet updates |
| OTA update | Field products | Requires partitions, network code, TLS, rollback, and release process |
| Single signing key | Simple demo | No real recovery from lost/leaked key |
| Multiple key slots | Production rotation | Must be planned before eFuses are locked |

## 14. Final Pre-Lock Checklist

Before enabling hardware Secure Boot on any board:

| Check | Required |
|---|---|
| Spare board available | Yes |
| Signing key generated and backed up | Yes |
| Key excluded from git | Yes |
| Stage 1 plain build tested | Yes |
| Stage 2 signed app tested | Yes |
| Bootloader room confirmed with `CONFIG_PARTITION_TABLE_OFFSET=0xD000` | Yes |
| OTA partition plan decided before production | Yes |
| Flash Encryption decision made | Yes |
| Recovery path understood | Yes |
| Success and failure tests reviewed | Yes |
| Known gaps and mitigation plan reviewed | Yes |

## 15. Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| App no longer boots after Secure Boot | Unsigned app or wrong key | Rebuild and flash app signed with trusted key |
| Old firmware rejected | Anti-rollback version too low | Build a newer signed firmware |
| Flash command works but boot fails | Bootloader rejected the image | Check signature and key |
| Cannot update with a new key | New key was not provisioned in eFuse | Use the original key or replace board |
| Device cannot be recovered | Bootloader/key/revoke mistake | Review eFuse state and provisioning logs |

## 16. Minimal Learning Path

For this repository, the recommended learning order is:

```text
1. Build and flash Stage 1
2. Change one print line and reupload
3. Generate ECDSA-256 signing key
4. Rehearse Stage 2 signed app
5. Inspect signature block
6. Read eFuse summary before locking
7. Enable Stage 3 only on a spare board
8. Rebuild a signed app and prove updates still work
9. Design OTA before using Secure Boot in production
10. Add Flash Encryption only after update flow is proven
11. Run the testing matrix and document pass/fail behavior
```
