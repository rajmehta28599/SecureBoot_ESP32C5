# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

A standalone **ESP-IDF** project used to learn, prototype, and prove out
**Secure Boot v2** (and Flash Encryption) on the **ESP32-C5** before those
features are applied to the production payroll firmware. It is intentionally
separate from the Arduino-based firmware in the sibling `Payroll_Firmware_*`
folders: Secure Boot is a build-system + bootloader concern that the
Arduino-ESP32 core does not expose, so this work is done in ESP-IDF.

The app itself (`main/secure_boot_demo_main.c`) is a **runtime status reporter**:
it prints a `STEP N of 3` banner followed by five blocks — `[1/5] DEVICE
IDENTITY`, `[2/5] FIRMWARE IMAGE`, `[3/5] RUNTIME HEALTH`, `[4/5] SECURITY
STATE`, `[5/5] eFUSE DETAIL` — reading the real eFuse bits via
`esp_secure_boot_enabled()` / `esp_efuse_is_flash_encryption_enabled()` (**not**
the v6.0-deprecated `esp_flash_encryption_enabled()`) plus read-only
`esp_efuse_*` queries. Its job is to make the "unprotected → locked" transition
observable, not to enable anything itself. **Nothing in it may call an
`esp_efuse_set_*` / `esp_efuse_write_*` function — those blow fuses.**

The `STEP N` in the banner is a **compile-time** value, not a runtime one:
`CONFIG_SECURE_BOOT` → 3, else `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` → 2,
else 1. It has to be, because steps 1 and 2 are indistinguishable at runtime —
software signing burns no eFuse. The gap between the banner and `[4/5]` in step
2 is the lesson.

**The three states = the three lab stages.** The same unchanged app is meant to
be flashed in three configurations so you can watch the report change (Stage N
in `README.md` produces State N):

| Build state | How you get there | App reports | Reversible? |
|---|---|---|---|
| A — plain | Stage 1, defaults as shipped | Secure Boot **DISABLED** | yes |
| B — signed app (software) | Stage 2, uncomment Stage-2 block / menuconfig | Secure Boot **DISABLED** (but app is signed) | yes |
| C — hardware Secure Boot | Stage 3, `CONFIG_SECURE_BOOT=y` + burn | Secure Boot **ENABLED** | **NO — eFuse burned** |

The lesson the demo teaches: signing an app (B) is *not* the same as locking the
hardware (C). Only C flips the eFuse the reporter reads.

**Code shape:** a single translation unit — `main/secure_boot_demo_main.c`,
registered in `main/CMakeLists.txt` with `PRIV_REQUIRES bootloader_support efuse
esp_app_format app_update esp_partition spi_flash esp_timer`. If you add code
that calls into another IDF component (nvs, esp_wifi, …), add it to that
`PRIV_REQUIRES` list or the build won't find the headers. Two traps worth
knowing: `bootloader_support` requires `efuse` only *privately*, so `efuse` does
**not** come along for free — it must be listed explicitly for any
`esp_efuse_*` call; and `esp_hw_support` / `freertos` / `log` / `heap` are
always available, so `esp_chip_info.h`, `esp_mac.h` and `esp_heap_caps.h` need
no entry at all.

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

Three settings every command depends on, none of which live where the docs say:

- **Target** — `CONFIG_IDF_TARGET="esp32c5"` in `sdkconfig.defaults`, applied by
  `idf.py set-target esp32c5`. It is *not* recorded in `.vscode/settings.json`.
- **Serial port** — `idf.portWin` in `.vscode/settings.json`, **currently `COM9`**.
  Every other port string in the repo says `COM15` and is stale — 66 of them,
  across `README.md`, the four `SECURE_BOOT_*.md` docs, `AGENTS.md`,
  `secure-boot-setup.html` and `secure-boot-cheatsheet.html`. Read
  `settings.json` for the live value before flashing; `<PORT>` below means it.
- **ESP-IDF install** — `idf.currentSetup` in `.vscode/settings.json`, **currently
  `C:\Users\rajme\esp\v6.0.2\esp-idf`**. The VS Code extension rewrites this key,
  so it moves: it pointed at **v6.0.1** until Aug 2026, and every "verified
  against ESP-IDF v6.0.1" claim in this repo was checked against that install
  (still on disk at `…\esp\v6.0.1\esp-idf`). When a version-specific detail
  matters, re-check it against the setup actually in use.

```bash
idf.py set-target esp32c5         # once
idf.py build                      # compile
idf.py -p <PORT> flash monitor    # flash + serial (exit monitor: Ctrl+])
idf.py fullclean                  # after editing sdkconfig.defaults — see below
idf.py -p <PORT> efuse-summary    # inspect what is / isn't burned (read-only, safe)
idf.py menuconfig                 # preferred way to touch Security features
idf.py secure-generate-signing-key --scheme ecdsa256 secure_boot_signing_key.pem
```

**`sdkconfig` is generated, git-ignored, and overrides `sdkconfig.defaults`.** A
stale one is sitting in the working tree, so editing the defaults file (e.g.
uncommenting the Stage-2 block) changes nothing until you `idf.py fullclean` or
delete `sdkconfig`. To find out which stage a tree is actually in, grep
`sdkconfig` in this order: `CONFIG_SECURE_BOOT=y` → **Stage 3**, a build that will
burn eFuses; else `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y` → **Stage 2**;
neither → **Stage 1** (plain — the state as of this writing). Ignore the
`_SUPPORTED` / `_PREFERRED` / `SECURE_ROM_DL_MODE_*` / `SECURE_TEE_*` symbols;
they are set in a plain build too and prove nothing.

⚠️ **Terminal build gotcha on this machine:** running `export.ps1`/`export.bat`
reports OK but does **not** put `riscv32-esp-elf-gcc` on `PATH`, so `idf.py
build` fails with *"CMAKE_C_COMPILER riscv32-esp-elf-gcc … was not found in the
PATH."* This is an environment defect, not a project bug. Reliable options:
- **Preferred:** use the **VS Code ESP-IDF extension's Build** button — it sets
  `PATH` itself (`.vscode/settings.json` is configured for it).
- **Repair the terminal:** `& "C:\Users\rajme\esp\v6.0.2\esp-idf\install.ps1" esp32c5`
  (that path is whatever `idf.currentSetup` says — it was `v6.0.1` before Aug 2026).
- **One-shot workaround:** after `. export.ps1`, prepend
  `C:\Users\rajme\.espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin`
  to `$env:PATH`, then `idf.py build`. (IDF v6.0.1 and v6.0.2 both pin that same
  toolchain version, so the path survives the setup switch.)
- Run `idf.py` / `idf_tools.py` in **PowerShell**, never the Bash tool — under
  MSys/Git-Bash they abort with *"MSys/Mingw is not supported."*
- The one-shot workaround is **verified working** (clean build, exit 0, as of
  Aug 2026) — see the stale-`build/` note below, which is what bites first.

⚠️ **Stale `build/` after the ESP-IDF setup moves.** When `idf.currentSetup`
changed from **v6.0.1** to **v6.0.2**, the `build/` tree left behind by the old
install made CMake abort mid-build:

```
CMake Error: The source "C:/Users/rajme/esp/v6.0.2/esp-idf/components/bootloader/subproject/CMakeLists.txt"
does not match the source "C:/Users/rajme/esp/v6.0.1/esp-idf/components/bootloader/subproject/CMakeLists.txt"
used to generate cache.
```

The CMake **cache itself** is the problem, so **delete `build/` outright**
(`Remove-Item -Recurse -Force build`) — it is git-ignored and fully
regenerable. Expect a full ~1100-step rebuild afterwards. Watch for this every
time the VS Code extension rewrites `idf.currentSetup`.

There is **no Arduino IDE build** for this folder. All `.c` sources live under
`main/` and are registered in `main/CMakeLists.txt`.

## Secure Boot facts for ESP32-C5 (verified against local ESP-IDF v6.0.1 docs)

- Secure Boot **v2** only. Schemes: **RSA-3072, ECDSA-384/256/192**. Default and
  recommended: **ECDSA (v2)**; this demo uses **ECDSA-256** (fast verify, short key).
- **ECDSA-P192 is a weak legacy curve (~96-bit) — prefer P-256 (default) or P-384.**
  The "disabled-by-default / curve-locks-on-Secure-Boot-enable" caveat some ESP-IDF
  docs mention is an **ESP32-H2/H21 trait, NOT the C5** (it's gated on
  `SOC_ECDSA_P192_CURVE_DEFAULT_DISABLED`, which the C5 does not set). Verified against
  local v6.0.1 `soc_caps.h`; see `SECURE_BOOT_DEEP_DIVE.md` §4.6.
- Up to **3 public-key digest slots** → key revocation/rotation is supported
  (`KEY_REVOKEx`). Sign an OTA with a new key, then revoke the old one.
- Relevant Kconfig: `CONFIG_SECURE_BOOT`, `CONFIG_SECURE_BOOT_V2_ECDSA_ENABLED`,
  `CONFIG_SECURE_BOOT_ECDSA_KEY_LEN_256_BITS`, `CONFIG_SECURE_BOOT_SIGNING_KEY`,
  `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` (software-only, reversible),
  `CONFIG_SECURE_FLASH_ENC_ENABLED`. Prefer `idf.py menuconfig` → *Security
  features* over hand-editing these — menuconfig enforces valid combinations.
- Secure Boot proves **authenticity**; Flash Encryption proves
  **confidentiality**. Production needs **both** (avoids a TOCTOU flash-swap).

## Flash layout, partition table, and what that rules out

Actual offsets, from `build/flasher_args.json` (2 MB flash, current Stage-1 build):

| Offset | Image |
|---|---|
| `0x2000` | `bootloader/bootloader.bin` |
| `0xD000` | `partition_table/partition-table.bin` |
| `0x20000` | `secure_boot_demo.bin` (the app) |

`CONFIG_PARTITION_TABLE_OFFSET=0xD000` (in `sdkconfig.defaults`) is deliberate:
a signed bootloader is larger (it carries a 1216-byte signature block + secure
padding), so the partition table is pushed out to give the bootloader room. Do
not shrink this back to the 0x8000 default when Secure Boot is in play.

`partitions.csv` declares **`factory` only** — no `otadata`, no `ota_0`/`ota_1` —
so **OTA is impossible in this project as it stands**; lab updates go over
USB/UART. Adding OTA is a partition-table change *plus* app work (`esp_https_ota`,
rollback handling, signed images); `SECURE_BOOT_IMPLEMENTATION_GUIDE.md` §9 and
`SECURE_BOOT_PRACTICAL_FLOW.md` §9 already sketch the layout — follow them rather
than inventing a new one.

**Stage-3 flashing gotcha:** with `CONFIG_SECURE_BOOT=y`, `idf.py flash` will
*not* flash the bootloader. The build prints an `esptool … write-flash 0x2000
build/bootloader/bootloader.bin` command to run by hand. That manual step is the
point of no return: the eFuses are burned by the **first boot** of that signed
bootloader, not by the flash command itself.

## Key safety

The signing key (`secure_boot_signing_key.pem`) is the root of trust for every
device that trusts it. It is git-ignored (`*.pem`). Never commit it, never print
its contents, and generate it on a trusted machine (or HSM) for production.

## The docs are the deliverable (and they duplicate each other)

This repo is ~100 lines of C and ~3,200 lines of Markdown plus three standalone
HTML pages. Most requests here are **documentation work, not firmware work.** Each
file teaches the same subject from a different angle:

| File | Owns |
|---|---|
| `README.md` | Theory writeup + the 3-stage lab (baseline → software signing → hardware Secure Boot). Point users here for steps. |
| `SECURE_BOOT_DEEP_DIVE.md` | Conceptual reference: chain of trust, ESP32-family matrix, signature schemes, eFuse/key layout & revocation, per-file data map, threat model, glossary. Fact-checked section-by-section against local ESP-IDF v6.0.1 docs. |
| `SECURE_BOOT_VISUAL_GUIDE.md` | Diagram-first map: what lives where, reupload/recovery, UART vs OTA, key rotation. |
| `SECURE_BOOT_IMPLEMENTATION_GUIDE.md` | Command-by-command checklist + OTA layout, testing scenarios, gap analysis, pre-lock checklist. |
| `SECURE_BOOT_PRACTICAL_FLOW.md` | Flow lab: per action → what changes on the device → expected result. |
| `secure-boot-flow.html` · `secure-boot-setup.html` · `secure-boot-cheatsheet.html` | Interactive boot-flow diagram · click-through staged setup lab · printable one-page command reference. Self-contained; no build step. |

**Sync rule:** the same fact is written down in five to eight places. Changing one
means grepping for it and updating *all* of them — the Kconfig stage blocks
(`sdkconfig.defaults` + deep dive §7 + implementation guide), the partition table
(three docs), the command tables (README + cheatsheet HTML + setup HTML), the
P-192/H2 correction (README §1.4 + this file + deep dive §4.6). The 66 stale
`COM15` strings are what happens when that rule is skipped.

## Conventions and verification

- **No test suite.** Verification is `idf.py build` plus reading the serial
  report — the `STEP N of 3` banner, the five `[n/5]` blocks, and within them
  `Secure Boot v2  : DISABLED/ENABLED`, `Flash Encrypt.  :`,
  `Flash Enc. mode :` and the `alive: secure_boot=… flash_enc=…` heartbeat every
  5 s. State the actual observed lines when reporting a change, not "it builds".
  Those four quoted strings are reproduced verbatim across README, the deep
  dive, the implementation and practical-flow guides and both HTML pages —
  **keep them byte-identical** and add around them rather than reformatting.
- **C style** — match `main/secure_boot_demo_main.c`: 4-space indent,
  `snake_case`, `static` file-local helpers, uppercase log tag (`SB_DEMO`), and
  comments that explain hardware/security behavior rather than C syntax.
- **`AGENTS.md` covers the same ground** (structure, commands, style, commit/PR
  expectations). It and this file overlap — change a rule in one, change it in
  the other. Commits are short and imperative; PRs must call out any Secure Boot,
  Flash Encryption, eFuse, partition, or signing-key impact.
