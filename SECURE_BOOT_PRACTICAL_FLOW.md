# Secure Boot Practical Flow Lab

This guide is the practical learning flow only. It shows what you do, what file or artifact changes, what the ESP32-C5 does, and how to know whether the step succeeded.

Use a spare board for any hardware Secure Boot step. Stage 3 burns eFuses permanently.

## 0. Lab Setup

Inputs:

| Item | Example |
|---|---|
| Target chip | `esp32c5` |
| Serial port | `COM15` |
| Main app file | `main/secure_boot_demo_main.c` |
| Signing key | `secure_boot_signing_key.pem` |
| Safe build command | `idf.py build` |
| Flash command | `idf.py -p COM15 flash monitor` |

Safety rule:

```text
Stage 1 = safe, unsigned, reversible
Stage 2 = safe, signed app rehearsal, reversible
Stage 3 = hardware Secure Boot, irreversible
```

## 1. Whole Implementation Flow

```text
Operator                  Build PC                  ESP32-C5 flash/eFuse              Device result
--------                  --------                  --------------------              -------------
edit C code          ->   compile app          ->   app image in flash           ->   app prints status
generate key         ->   .pem private key     ->   no device change             ->   nothing burned
enable signing       ->   signed app image     ->   signed app in flash          ->   signature can verify
enable Secure Boot   ->   signed bootloader    ->   eFuse key digest + enable    ->   unsigned code rejected
future update        ->   signed new app       ->   app partition updated        ->   boots only if trusted
```

The device trusts the key, not the file name:

```text
trusted key + changed code = accepted
wrong key + same code      = rejected
no signature + any code    = rejected after Secure Boot
```

## 2. Flow A: Build The Plain Demo

Command:

```powershell
idf.py set-target esp32c5
idf.py build
idf.py -p COM15 flash monitor
```

What happens:

| Area | What happens |
|---|---|
| Source | `main/secure_boot_demo_main.c` is compiled |
| Build output | `build/secure_boot_demo.bin` is created |
| Flash | Bootloader, partition table, and app are written |
| eFuse | No Secure Boot eFuse is burned |
| Device | Runs the app without signature enforcement |

Expected monitor learning point:

```text
Secure Boot v2  : DISABLED
Flash Encrypt.  : DISABLED
alive: secure_boot=0 flash_enc=0
```

Visual:

```text
C source -> idf.py build -> unsigned app -> UART flash -> boots
```

Success means the board, port, ESP-IDF environment, and app are working.

## 3. Flow B: Change Code And Reupload

Make a small visible change in `main/secure_boot_demo_main.c`, for example change one `printf()` label or add a new `ESP_LOGI()` line.

Then run:

```powershell
idf.py build
idf.py -p COM15 flash monitor
```

What happens:

```text
changed C code
   |
   v
new app binary
   |
   v
same board flash
   |
   v
new serial output
```

Result by security state:

| Device state | Changed app result |
|---|---|
| Secure Boot off | Boots even if unsigned |
| Stage 2 signed app | Boots if signing config is valid |
| Hardware Secure Boot on | Boots only if signed by trusted key |

This is the most important practical lesson: after Secure Boot, code changes are still allowed, but the new firmware must be signed correctly.

## 4. Flow C: Generate The Signing Key

Command:

```powershell
idf.py secure-generate-signing-key --scheme ecdsa256 secure_boot_signing_key.pem
```

What happens:

| Area | What happens |
|---|---|
| PC | A private ECDSA-256 key file is created |
| Device | Nothing changes yet |
| Flash | Nothing changes yet |
| eFuse | Nothing changes yet |

Visual:

```text
private key on PC
        |
        | signs future firmware
        v
signature block in app or bootloader
```

Never commit this key. If a locked device trusts this key and you lose it, that device cannot receive newly signed firmware.

## 5. Flow D: Stage 2 Signed App Rehearsal

Goal: learn signing without locking hardware.

Enable Stage 2 through `idf.py menuconfig` or the commented Stage 2 block in `sdkconfig.defaults`:

```text
Require signed app images
App Signing Scheme: ECDSA v2
ECDSA key length: 256
Signing key: secure_boot_signing_key.pem
```

Commands:

```powershell
idf.py fullclean
idf.py build
idf.py -p COM15 flash monitor
espsecure signature-info-v2 build/secure_boot_demo.bin
```

What happens:

| Area | What happens |
|---|---|
| Build | ESP-IDF appends a Secure Boot v2 signature block |
| App image | Contains app data, padding, public key, hash, signature, CRC |
| eFuse | Still unchanged |
| Device | Still reports hardware Secure Boot disabled |

Visual:

```text
app.bin before signing
        |
        v
+------------------+
| app firmware     |
+------------------+

app.bin after signing
        |
        v
+------------------+
| app firmware     |
+------------------+
| secure padding   |
+------------------+
| signature block  |
+------------------+
```

Expected learning result:

```text
The app can now prove who signed it.
The chip is not permanently locked yet.
```

## 6. Flow E: Stage 3 Hardware Secure Boot

Only do this on a spare board.

In `idf.py menuconfig`:

```text
Enable hardware Secure Boot in bootloader
Secure Boot v2
ECDSA v2
ECDSA key length: 256
Signing key: secure_boot_signing_key.pem
```

Build the signed bootloader:

```powershell
idf.py bootloader
```

ESP-IDF prints an `esptool write-flash ... bootloader.bin` command. Run the exact printed command for your port and board.

Then flash the signed app and monitor:

```powershell
idf.py -p COM15 flash monitor
```

What happens on first secure boot:

```text
Power on
   |
   v
ROM starts unsigned first-stage code from chip ROM
   |
   v
2nd-stage bootloader runs for the first secure provisioning boot
   |
   v
bootloader burns public-key digest into eFuse
   |
   v
bootloader burns SECURE_BOOT_EN
   |
   v
device is now locked to trusted signed images
```

After that:

```text
Power on
   |
   v
ROM checks SECURE_BOOT_EN
   |
   v
ROM verifies bootloader signature
   |
   v
bootloader verifies app signature
   |
   v
app runs only if trusted
```

Confirm:

```powershell
idf.py -p COM15 efuse-summary
```

Look for:

```text
SECURE_BOOT_EN = 1
BLOCK_KEYx contains Secure Boot digest
```

## 7. Flow F: Update After Lock

Practical update flow after Secure Boot:

```text
edit code
   |
   v
idf.py build
   |
   v
signed app generated
   |
   v
idf.py -p COM15 flash monitor
   |
   v
bootloader accepts only if signature is trusted
```

Success and failure:

| Action | Result |
|---|---|
| Rebuild with same trusted key | Boots |
| Flash unsigned app | Rejected |
| Flash app signed by different key | Rejected |
| Flash old signed app | Boots unless anti-rollback blocks it |
| Flash bad bootloader | High brick risk |

Practical rule:

```text
App updates are normal.
Bootloader updates are dangerous after lock.
```

## 8. Flow G: Failure Learning Scenarios

Use this section to understand behavior. Do not intentionally break a locked production board.

| Scenario | Safe stage | Expected behavior |
|---|---|---|
| Wrong COM port | Any stage | Flash command fails before touching firmware |
| Syntax error in C | Any stage | `idf.py build` fails, device unchanged |
| Missing signing key | Stage 2 or 3 build | Build fails, device unchanged |
| Unsigned app | Stage 1 | Boots |
| Unsigned app | Stage 3 | Bootloader rejects |
| Wrong key app | Stage 3 | Bootloader rejects |
| Modified signed binary | Stage 3 | Hash/signature check fails |
| Lost private key | After Stage 3 | Future updates cannot be signed |
| Revoked only valid key | After Stage 3 | Device cannot trust future images |

Failure decision tree:

```text
failure happened before flash?
        |
        +-- yes -> fix build/config, board unchanged
        |
        v
failure happened after app flash?
        |
        +-- yes -> flash valid signed app again
        |
        v
failure involved bootloader or eFuse?
        |
        +-- yes -> board may be permanently locked or bricked
```

## 9. Flow H: OTA Practical Implementation

Your current partition table is factory-only. OTA needs a different layout before production lock.

Practical OTA layout:

```csv
# Name,     Type, SubType,  Offset,  Size,   Flags
nvs,        data, nvs,      ,        0x6000,
otadata,    data, ota,      ,        0x2000,
phy_init,   data, phy,      ,        0x1000,
ota_0,      app,  ota_0,    ,        1M,
ota_1,      app,  ota_1,    ,        1M,
```

OTA runtime flow:

```text
running app in ota_0
        |
        v
downloads signed new app
        |
        v
writes new app to ota_1
        |
        v
sets ota_1 as next boot
        |
        v
reboot
        |
        v
bootloader verifies ota_1 signature
        |
        +-- success -> run ota_1
        |
        +-- fail -> stay with or roll back to previous valid slot
```

What you must implement:

| Part | Purpose |
|---|---|
| Wi-Fi or Ethernet | Network path to update server |
| HTTPS certificate validation | Confirms the server is trusted |
| `esp_https_ota` | Downloads and writes firmware |
| OTA partition table | Gives old and new apps separate slots |
| Rollback confirmation | Prevents broken firmware from becoming permanent |
| Version checks | Blocks unwanted downgrade |
| Signed images | Required by Secure Boot |

## 10. Flow I: Key Rotation Practical View

Key rotation works only if extra trusted key slots were provisioned before locking.

Good production setup:

```text
eFuse slot 0 -> digest of key_0
eFuse slot 1 -> digest of key_1
eFuse slot 2 -> digest of key_2
```

Rotation:

```text
old app signed by key_0
        |
        v
OTA new app signed by key_1
        |
        v
device accepts key_1 because slot 1 is trusted
        |
        v
new app boots and passes self-test
        |
        v
new app revokes key_0
        |
        v
future key_0 firmware is rejected
```

Bad setup:

```text
only key_0 trusted
        |
        v
key_0 lost or leaked
        |
        v
no safe way to move fleet to key_1
```

## 11. Flow J: Flash Encryption Practical View

Secure Boot and Flash Encryption solve different problems.

```text
Secure Boot:
  can this code run?

Flash Encryption:
  can someone read the flash contents?
```

Practical result:

| Setup | Attacker flashes fake app | Attacker reads flash |
|---|---|---|
| No security | Fake app runs | Plaintext visible |
| Secure Boot only | Fake app rejected | Plaintext may be visible |
| Flash Encryption only | Fake app risk depends on boot policy | Encrypted contents |
| Both | Fake app rejected | Encrypted contents |

Use Development mode while learning. Use Release mode only after OTA and recovery are proven.

## 12. Final Practical Learning Script

Run the lab in this order:

```text
1. Build Stage 1 and confirm status prints
2. Change one log or print line and reupload
3. Generate ECDSA-256 signing key
4. Enable Stage 2 signing and inspect signature block
5. Confirm eFuse still says Secure Boot disabled
6. Decide whether this is a sacrificial board
7. Enable Stage 3 only on that spare board
8. Confirm SECURE_BOOT_EN with efuse-summary
9. Rebuild a small signed code change and flash it
10. Confirm the changed signed app boots
11. Learn failure behavior with app-only tests
12. Design OTA before production use
13. Add Flash Encryption only after update flow is proven
14. Plan key slots before locking a fleet
```

The practical takeaway:

```text
Build proves code compiles.
Signing proves firmware came from your key.
Secure Boot proves the chip enforces that key.
OTA proves devices can update safely in the field.
Flash Encryption proves flash contents are not exposed.
Key rotation proves you can recover from a key leak.
```

