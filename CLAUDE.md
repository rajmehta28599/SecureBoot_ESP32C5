# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

A standalone **ESP-IDF v6.0.1** project used to learn, prototype, and prove out
**Secure Boot v2** (and Flash Encryption) on the **ESP32-C5** before those
features are applied to the production payroll firmware. It is intentionally
separate from the Arduino-based firmware in the sibling `Payroll_Firmware_*`
folders: Secure Boot is a build-system + bootloader concern that the
Arduino-ESP32 core does not expose, so this work is done in ESP-IDF.

The app itself (`main/secure_boot_demo_main.c`) is a **runtime status reporter**:
it reads the real eFuse bits via `esp_secure_boot_enabled()` /
`esp_flash_encryption_enabled()` and prints whether the security features are ON
or OFF. Its job is to make the "unprotected → locked" transition observable, not
to enable anything itself.

## ⚠️ Irreversible-hardware rule (most important thing here)

Enabling `CONFIG_SECURE_BOOT` or Flash Encryption **burns eFuses, which are
one-time-programmable and can never be undone.** A locked board will only run
images signed with the project's key; losing the key bricks updates forever.
- **Never** enable Stage 3 (hardware Secure Boot) on a board that matters —
  spare/dev boards only.
- **Never** run `idf.py efuse-burn` / `efuse-burn-key` or flash a
  `CONFIG_SECURE_BOOT=y` build without the user's explicit, board-aware go-ahead.
- Stages 1–2 (see `README.md`) are reversible; prefer them for iteration.

## Build / flash / debug (ESP-IDF, not Arduino IDE)

Run from an ESP-IDF terminal (after `export.bat`). Target and port are already
recorded in `.vscode/settings.json` (ESP-IDF v6.0.1, `COM15`).

```bash
idf.py set-target esp32c5        # once
idf.py build                     # compile
idf.py -p COM15 flash monitor    # flash + serial (exit monitor: Ctrl+])
idf.py fullclean                 # after editing sdkconfig.defaults
idf.py -p COM15 efuse-summary    # inspect what is / isn't burned
```

There is **no Arduino IDE build** for this folder. All `.c` sources live under
`main/` and are registered in `main/CMakeLists.txt`.

## Secure Boot facts for ESP32-C5 (verified against local ESP-IDF v6.0.1 docs)

- Secure Boot **v2** only. Schemes: **RSA-3072, ECDSA-384/256/192**. Default and
  recommended: **ECDSA (v2)**; this demo uses **ECDSA-256** (fast verify, short key).
- **ECDSA-P192 is disabled by default** on the C5 and the curve mode locks once
  Secure Boot is enabled — use P-256 (or P-384) unless P-192 was set beforehand.
- Up to **3 public-key digest slots** → key revocation/rotation is supported
  (`KEY_REVOKEx`). Sign an OTA with a new key, then revoke the old one.
- Relevant Kconfig: `CONFIG_SECURE_BOOT`, `CONFIG_SECURE_BOOT_V2_ECDSA_ENABLED`,
  `CONFIG_SECURE_BOOT_ECDSA_KEY_LEN_256_BITS`, `CONFIG_SECURE_BOOT_SIGNING_KEY`,
  `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` (software-only, reversible),
  `CONFIG_SECURE_FLASH_ENC_ENABLED`. Prefer `idf.py menuconfig` → *Security
  features* over hand-editing these — menuconfig enforces valid combinations.
- Secure Boot proves **authenticity**; Flash Encryption proves
  **confidentiality**. Production needs **both** (avoids a TOCTOU flash-swap).

## Partition-table note

`CONFIG_PARTITION_TABLE_OFFSET=0xD000` (in `sdkconfig.defaults`) is deliberate:
a signed bootloader is larger (it carries a 1216-byte signature block + secure
padding), so the partition table is pushed out to give the bootloader room. Do
not shrink this back to the 0x8000 default when Secure Boot is in play.

## Key safety

The signing key (`secure_boot_signing_key.pem`) is the root of trust for every
device that trusts it. It is git-ignored (`*.pem`). Never commit it, never print
its contents, and generate it on a trusted machine (or HSM) for production.

## Hands-on lab

`README.md` contains the full theory writeup and the 3-stage lab
(baseline → software signed-app verification → hardware Secure Boot). Point users
there rather than duplicating steps.
