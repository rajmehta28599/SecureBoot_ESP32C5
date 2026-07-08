# ESP32-C5 Secure Boot Visual Guide

This guide explains how the demo firmware, Secure Boot, USB/UART flashing, OTA, Flash Encryption, and signing keys fit together.

## 1. Normal Development Before Secure Boot

Before hardware Secure Boot is enabled, the chip will run almost anything you flash.

```text
Edit code
   |
   v
main/secure_boot_demo_main.c
   |
   v
idf.py build
   |
   v
build/secure_boot_demo.bin
   |
   v
idf.py -p COM15 flash monitor
   |
   v
ESP32-C5 boots the app
```

Behavior before Secure Boot:

| Firmware image | Result |
|---|---|
| Original demo app | Boots |
| Modified demo app | Boots |
| Unsigned app | Boots |
| Random compatible app | Boots |

This is useful for learning and development, but it is not secure for production.

## 2. What Secure Boot v2 Checks

Secure Boot does not ask whether the firmware file name is correct. It asks two stricter questions:

1. Was this image signed by a trusted private key?
2. Was the image changed after it was signed?

```text
Build PC or signing server                  ESP32-C5 device
--------------------------                  ----------------

private signing key
secure_boot_signing_key.pem
        |
        | signs firmware
        v
signed app image ----------------------->   flash memory
                                             |
                                             v
                                   bootloader verifies signature
                                             |
                            valid? ----------+---------- invalid?
                              |                             |
                              v                             v
                            boot                         reject boot
```

The private key stays on the PC, build server, or HSM. The ESP32-C5 stores only a public-key digest in eFuse.

## 3. What Lives Where

| Location | Contents | Secret? |
|---|---|---|
| Build PC | Source code, ESP-IDF tools, private signing key | Private key is secret |
| ESP32-C5 eFuse | Secure Boot enable bit, public-key digest, revoke bits | No private key |
| ESP32-C5 flash | Bootloader, partition table, app, signature block | Plaintext unless Flash Encryption is enabled |

Signed app layout:

```text
+--------------------+
| App firmware       |
| compiled from C    |
+--------------------+
| Secure padding     |
+--------------------+
| Signature block    |
| public key         |
| image hash         |
| digital signature  |
| CRC                |
+--------------------+
```

## 4. Boot Flow After Secure Boot Is Enabled

```text
Power on
   |
   v
ROM bootloader inside chip
   |
   v
Read Secure Boot eFuse
   |
   v
Secure Boot enabled?
   |
   +-- No  -> run bootloader normally
   |
   +-- Yes
        |
        v
Verify 2nd-stage bootloader signature
        |
        +-- invalid -> stop boot
        |
        v
Run trusted bootloader
        |
        v
Verify app signature
        |
        +-- invalid -> stop boot
        |
        v
Run app
```

Accepted and rejected cases:

| Image | Result after Secure Boot |
|---|---|
| Bootloader signed by trusted key | Accepted |
| App signed by trusted key | Accepted |
| Unsigned app | Rejected |
| App signed with wrong key | Rejected |
| Signed app modified after signing | Rejected |

## 5. Changing Code And Reuploading

Changing the code is allowed. The new image only needs to be signed with a trusted key.

```text
Change C code
   |
   v
idf.py build
   |
   v
ESP-IDF signs the new app using the configured private key
   |
   v
idf.py -p COM15 flash monitor
   |
   v
Bootloader verifies the signature
   |
   v
App boots if the signature is trusted
```

Rule:

```text
Different code + correct trusted signature = boots
Same code + no trusted signature           = rejected
Wrong code + wrong key                     = rejected
Old valid code + trusted signature         = boots unless anti-rollback blocks it
```

## 6. Uploading Other Code And Recovering

If Secure Boot is enabled and you upload other unsigned or wrongly signed firmware, flashing may still appear to succeed, but boot will fail.

| What was changed | Can you recover by flashing valid firmware again? | Notes |
|---|---|---|
| Only the app was replaced | Usually yes | Flash a correctly signed app again |
| USB/UART download still allowed | Usually yes | Use the correct ESP-IDF flashing flow |
| Bootloader was replaced incorrectly | Risky | A bad Secure Boot bootloader can brick the board |
| Anti-rollback rejects old version | No, not that old version | Build a newer signed version |
| Flash Encryption release mode is locked | Maybe not over UART | Use the planned OTA or encrypted update path |

Do not casually reflash the bootloader after enabling Secure Boot. A bad app is often recoverable. A bad trusted bootloader is much more dangerous.

## 7. USB/UART Flashing Versus OTA

### USB/UART

USB/UART is the simple development path.

```text
PC
 |
 | idf.py -p COM15 flash monitor
 v
ESP32-C5 flash
 |
 v
Bootloader verifies app
 |
 v
Run app if signed correctly
```

| Use case | USB/UART suitability |
|---|---|
| Lab testing | Good |
| First flashing | Good |
| Factory provisioning | Good |
| Remote deployed products | Poor |

### OTA

OTA updates are performed by the running firmware over the network.

```text
Running app
   |
   v
Download new signed firmware from server
   |
   v
Write image to ota_0 or ota_1
   |
   v
Mark new slot as boot target
   |
   v
Reboot
   |
   v
Bootloader verifies new app
   |
   +-- valid   -> boot new firmware
   |
   +-- invalid -> reject or roll back
```

This repository currently has only a `factory` app partition, so OTA is not enabled. OTA requires:

```text
otadata partition
ota_0 partition
ota_1 partition
OTA update code, for example esp_https_ota
rollback handling
version management
signed app images
```

## 8. Secure Boot Versus Flash Encryption

| Feature | Main purpose | Protects against | Does not protect against |
|---|---|---|---|
| Secure Boot | Authenticity and integrity | Fake or modified firmware running | Someone reading plaintext flash |
| Flash Encryption | Confidentiality | Reading firmware or secrets from flash | Trusting firmware by itself |
| Both together | Production-grade firmware protection | Tampering, fake code, flash readout | Bad key handling or unsafe provisioning |

```text
Secure Boot:
  only signed code can run

Flash Encryption:
  flash contents cannot be read as plaintext
```

For production, plan to use both.

## 9. Can The Key Be Changed?

The already-burned eFuse digest cannot be edited. Key rotation is possible only if extra trusted key slots were provisioned before the device was locked.

Single-key setup:

```text
slot 0: key_0 digest active
slot 1: revoked or unused
slot 2: revoked or unused
```

| Situation | Result |
|---|---|
| You still have `key_0.pem` | You can keep signing updates |
| You lose `key_0.pem` | The device cannot accept newly signed firmware |
| You generate `key_1.pem` later | Device rejects it |
| `key_0.pem` leaks | You cannot retire it if no spare slot is trusted |

Planned rotation setup:

```text
slot 0: key_0 digest active
slot 1: key_1 digest active
slot 2: key_2 digest active
```

```text
Current app signed by key_0
   |
   v
OTA app signed by key_1
   |
   v
Device verifies key_1 because slot 1 is trusted
   |
   v
New app boots
   |
   v
New app revokes key_0
   |
   v
Only key_1 remains trusted
```

After revocation:

| Firmware signed by | Result |
|---|---|
| Revoked `key_0` | Rejected |
| Active `key_1` | Accepted |
| Unknown key | Rejected |

For this demo project, keep `secure_boot_signing_key.pem` backed up and private. Do not change signing keys after locking Secure Boot unless the device was provisioned with multiple trusted key slots.

## 10. Practical Rule

Think of the device as trusting the signing key, not the filename or old firmware.

```text
same code + no signature        -> rejected
different code + correct key    -> accepted
same old code + correct key     -> accepted
wrong code + wrong key          -> rejected
```

