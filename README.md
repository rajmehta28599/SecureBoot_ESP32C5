# ESP32-C5 Secure Boot v2 — Theory + Hands-On Lab

A self-contained ESP-IDF project for **learning how Secure Boot works** on the
ESP32-C5, both conceptually and in code. Flash the demo app in three escalating
stages and watch a device go from "runs anything" to "cryptographically locked".

> ⚠️ **Read the [Irreversibility](#-the-one-way-door-read-this-first) section
> before Stage 3.** Enabling hardware Secure Boot **burns eFuses and cannot be
> undone.** Learn on a spare/dev board, not on a production device.

📖 **New here?** The [`SECURE_BOOT_DEEP_DIVE.md`](./SECURE_BOOT_DEEP_DIVE.md) companion is a
one-stop, first-timer-friendly walkthrough — boot-flow diagrams, the full ESP32-family
support matrix, signature schemes, eFuse/key internals, a per-file map, the threat model,
and a glossary of every term. For a practical visual map of code changes, USB/UART
flashing, OTA, Flash Encryption, and key rotation, see
[`SECURE_BOOT_VISUAL_GUIDE.md`](./SECURE_BOOT_VISUAL_GUIDE.md). For a command-by-command
implementation checklist with OTA and key-rotation snippets, see
[`SECURE_BOOT_IMPLEMENTATION_GUIDE.md`](./SECURE_BOOT_IMPLEMENTATION_GUIDE.md). This README
is the theory plus the 3-stage hands-on lab.

---

## Part 1 — Theory

### 1.0 In plain English — start here (no crypto background needed)

Imagine your firmware is a **letter of orders** sent to a soldier (the chip). The
soldier will do *whatever the letter says*. So how does the soldier know the
letter is really from the king, and not a forgery slipped in by an enemy?

The king uses a **signet ring** to press a **wax seal** into every genuine
letter. The ring is unique and stays locked in the palace — it **never** leaves.
The soldier doesn't have the ring, but he *has memorized what a genuine seal
looks like*. Before obeying any letter, he checks the seal:

- **Seal matches** → the orders are genuine → obey them.
- **Seal missing, broken, or wrong** → forgery → **burn the letter, do nothing.**

Map that onto the ESP32-C5:

| Story | Secure Boot reality |
|---|---|
| The king's **signet ring** | your **private signing key** (`*.pem`) — never on the device |
| The **wax seal** on a letter | the **signature block** appended to each firmware image |
| The soldier **remembering** the seal's look | the **public-key digest burned into eFuse** (one-time, permanent) |
| Soldier **refuses a bad seal** | the ROM/bootloader **refuses to boot** unsigned/tampered firmware |

Two things fall straight out of the analogy, and they're the whole point:

1. **Even if the enemy captures the soldier, they still can't forge orders** —
   the soldier only ever knew what the seal *looks like*, never how to *make* it.
   Likewise, a fully hacked device can't leak the signing key, because the key
   was never on it. Only a *hash of the public key* is.
2. **The seal proves who wrote the letter — it does NOT hide the letter's
   contents.** Anyone can still *read* the orders. Secure Boot is about
   **authenticity**, not secrecy. If you also need the text unreadable, that's a
   *second, separate* wax-lined envelope → **Flash Encryption** (§1.6).

That's the intuition. §1.2–§1.3 show the exact chain of checks; §1.7 covers when
this is worth doing (and when it's overkill).

### 1.1 What problem does Secure Boot solve?

Secure Boot answers one question at every power-on:

> "Is the code about to run **exactly** the code the manufacturer signed?"

Without it, anyone who can write to your flash (physical access, a malicious OTA,
a compromised update server) can replace your firmware with their own and the
chip will happily run it. Secure Boot makes the **hardware itself** refuse to
execute any code that isn't signed by *your* private key.

- **Secure Boot = authenticity + integrity** ("only *my* code runs, unmodified").
- It does **not** hide your code. For confidentiality (stopping someone from
  *reading* your firmware/keys out of flash) you need **Flash Encryption** — a
  separate feature. Production devices use both. See §1.6.

### 1.2 The chain of trust

The security rests on an anchor that physically cannot be changed — the **ROM
bootloader** (first-stage), which is mask-programmed into the silicon. Each link
verifies the next before handing over control:

```
  ┌─────────────────────┐
  │  ROM bootloader      │  immutable, in silicon  ── the root of trust
  │  (first stage)       │
  └──────────┬──────────┘
             │ 1. reads SECURE_BOOT_EN eFuse. If set:
             │ 2. verifies 2nd-stage bootloader's SIGNATURE against
             │    the PUBLIC-KEY DIGEST stored in eFuse
             ▼   (fail → abort boot)
  ┌─────────────────────┐
  │  2nd-stage           │  in flash, signed by YOUR private key
  │  bootloader          │
  └──────────┬──────────┘
             │ 3. verifies the selected APP image's signature
             ▼   (fail → try other OTA slot → else abort)
  ┌─────────────────────┐
  │  Application         │  in flash, signed by YOUR private key
  │  (your firmware)     │
  └─────────────────────┘
```

The **private key never touches the device.** Only a *digest (hash) of the
public key* is burned into eFuse. That means even a fully compromised device
cannot leak the secret needed to forge new firmware.

### 1.3 How verification actually works (Secure Boot v2)

Each signed image (bootloader and app) gets a **1216-byte signature block**
appended, aligned to a 4 KB boundary. It contains:

- a SHA hash of the image,
- the **public key**,
- the **signature** over the hash,
- a CRC32.

At boot, the verifier does three checks (from the ESP-IDF v2 spec):

1. **Is this a trusted key?** Hash the public key embedded in the signature
   block and compare it to the digest(s) burned in eFuse. No match → reject.
2. **Is the image intact?** Recompute the image hash and compare it to the hash
   in the signature block. Mismatch → reject.
3. **Is the signature genuine?** Use the (now-trusted) public key to verify the
   signature over the hash (RSA-PSS or ECDSA). Fail → reject.

Only if all three pass does control transfer.

### 1.4 ESP32-C5 specifics (verified against your ESP-IDF v6.0.1 docs)

| Property | ESP32-C5 value |
|---|---|
| Secure Boot version | **v2** only |
| Signature schemes | **RSA-3072, ECDSA-384, ECDSA-256, ECDSA-192** |
| Default / recommended | **ECDSA (v2)** — fast verify *and* short keys |
| ECDSA-P192 | weak legacy curve (~96-bit) — prefer P-256 / P-384. *(The "disabled-by-default, curve-locks-on-enable" caveat is an H2/H21 trait, **not** the C5.)* |
| Verify time @48 MHz ROM | RSA-3072 ≈ 12.1 ms · ECDSA-P256 ≈ 5.6 ms · ECDSA-P384 ≈ 20.6 ms |
| Public-key digest slots | **up to 3** (key #0, #1, #2) → supports key **revocation** |
| Key eFuses | `SECURE_BOOT_EN`, `KEY_PURPOSE_x`, `BLOCK_KEYx`, `KEY_REVOKEx` |

Because the C5 supports 3 key slots, you can **rotate/revoke** a compromised key
in the field without bricking devices — sign an OTA with a new key, then burn
`KEY_REVOKEx` to kill the old one (conservative approach in the ESP-IDF docs).
On this chip, ECDSA-256 is the sweet spot for a demo: fastest verify, small key.

### 1.5 Why this project uses ESP-IDF, not Arduino

Secure Boot is configured at **build-system + bootloader** level (signing keys,
eFuse policy, bootloader signature blocks). The Arduino-ESP32 core does not
expose this pipeline; ESP-IDF does (`idf.py secure-*`, menuconfig → *Security
features*). That is why this folder is a standalone ESP-IDF v6.0.1 project even
though the main payroll firmware is Arduino-based — this is the **sandbox** where
the secure-boot workflow is proven before it's applied to the product.

### 1.6 Secure Boot vs Flash Encryption (don't confuse them)

| | Secure Boot | Flash Encryption |
|---|---|---|
| Guarantees | Authenticity (only signed code runs) | Confidentiality (flash unreadable) |
| Stops | Running attacker's firmware | Reading out your firmware/keys |
| Key on device | Public-key **digest** (not secret) | AES key (secret, hardware-only) |
| Alone is enough? | No — see below | No |

Using Secure Boot **without** Flash Encryption leaves a *time-of-check/time-of-use*
gap: an attacker can swap flash contents after verification. **Espressif
recommends enabling both together** for production.

### 1.7 When to use it — use cases, pros & cons

**Where it earns its keep (real use cases):**

- **Devices you ship and can't physically guard** — anything in the field where
  an attacker (or a competitor) can get their hands on the hardware and its flash.
  *This payroll/attendance device is exactly that case:* it lives on a customer
  wall, handles biometric + payroll data, and updates over the air.
- **Anti-clone / anti-counterfeit** — stops someone dumping your firmware and
  running it on cheaper copycat hardware, or selling "compatible" clones.
- **Trustworthy OTA** — a compromised update server or a man-in-the-middle can't
  push malicious firmware; an unsigned image simply won't boot.
- **Compliance / regulated products** — payment, medical, and industrial
  standards increasingly *require* a hardware root of trust.

**Pros**

- **Rooted in hardware, not software** — the check lives in mask-ROM that can't
  be patched out. You can't "jailbreak" it with a software bug.
- **The secret never ships** — only a public-key *digest* is on the device, so
  even a fully compromised unit can't be used to forge firmware for the fleet.
- **Key rotation/revocation** — the C5's 3 digest slots let you retire a leaked
  key in the field (sign an OTA with a new key, then revoke the old) instead of
  scrapping every board.
- **Cheap at runtime** — ECDSA-P256 verify is ≈ 5.6 ms; you pay it once per boot.

**Cons / trade-offs (know these before Stage 3)**

- **Irreversible & brick-prone** — enabling it burns eFuses forever. **Lose the
  signing key and those boards can never be updated again.** A botched signing on
  a future OTA bricks every device that receives it.
- **Real key-management burden** — you now need a securely stored (ideally HSM-
  backed) private key, a signing step in every release, and a backup strategy.
  The key is the crown jewels for the whole fleet.
- **Harder to debug / recover** — enabling it disables the USB-OTG ROM download
  path and (by default) JTAG; UART download can be locked too. Field recovery of
  a wedged board gets much harder.
- **Build/layout overhead** — the signed bootloader grows (1216-byte signature
  block + padding), which is why the partition table is pushed to `0xD000`
  (see the partition-table note). Slightly slower, slightly bigger boot.
- **Authenticity only** — it does **nothing** to stop someone *reading* your
  flash. Pair it with Flash Encryption (§1.6) or your keys/code are still exposed.

**When it's overkill — skip it if:**

- The board is a **hobby/prototype/dev unit** you reflash constantly (you'd fight
  the lock every day, and you can't undo it).
- There's **no attacker incentive** — nothing secret, no fleet, no clone risk.
- You **can't commit to key management** yet. A lost key on a locked device is
  worse than no Secure Boot at all. Rehearse with **Stage 2 (software signing,
  reversible)** until the release process is solid, *then* commit hardware.

**Rule of thumb:** prototype and prove the whole pipeline with Stages 1–2
(reversible), and only burn eFuses (Stage 3) once (a) you have a backed-up,
access-controlled key and (b) you're on a board you accept losing.

---

## Part 2 — Hands-On Lab

> 🧪 **Prefer a visual, click-through version?** [`secure-boot-setup.html`](./secure-boot-setup.html)
> walks these same three stages with copy-ready commands, simulated serial output, a menuconfig
> screen, and an eFuse before/after. The steps below are the text reference.

### 🚪 The one-way door — READ THIS FIRST

- **`idf.py efuse-*` and enabling `CONFIG_SECURE_BOOT` burn eFuses. eFuses are
  one-time-programmable. There is no "undo", no "erase", no factory reset.**
- Once Secure Boot is on, the board will **only** run images signed with your
  key. Lose the key → you can never update that board again.
- Enabling Secure Boot **disables the USB-OTG ROM stack** and (by default)
  **JTAG**.
- **Stages 1–2 below are 100% reversible** (no eFuse writes). Do them first.
  Only do **Stage 3** on a board you are willing to lock permanently.

### Prerequisites

```bash
# From the ESP-IDF terminal (export.bat already run), in this folder:
idf.py set-target esp32c5
```

Your `.vscode/settings.json` already points at ESP-IDF v6.0.1 and `COM15`.

---

### Stage 1 — Baseline: see an UNPROTECTED device

Build and flash the demo as-is (no signing, no eFuses):

```bash
idf.py build
idf.py -p COM15 flash monitor
```

Expected output:

```
  Secure Boot v2  : DISABLED (device will run unsigned code)
  Flash Encrypt.  : DISABLED
```

**Lesson:** nothing is protecting this board yet. `esp_secure_boot_enabled()`
(in `main/secure_boot_demo_main.c`) reads the real eFuse bit and reports `false`.

---

### Stage 2 — Signed-app verification (SOFTWARE, still reversible)

Rehearse the signing workflow **without** touching eFuses. This signs the app
and would verify signatures on OTA, but the hardware stays unlocked.

1. Generate an ECDSA-256 signing key (keep this file secret; never commit it):

   ```bash
   idf.py secure-generate-signing-key --scheme ecdsa256 secure_boot_signing_key.pem
   ```

2. In `sdkconfig.defaults`, uncomment the **Stage 2** block (or run
   `idf.py menuconfig` → *Security features* → enable *Require signed app
   images* / *Bootloader verifies app signatures*). Then:

   ```bash
   idf.py fullclean && idf.py build
   idf.py -p COM15 flash monitor
   ```

3. Inspect the signature that was appended to your app:

   ```bash
   espsecure signature-info-v2 build/secure_boot_demo.bin
   ```

**Lesson:** the app is now cryptographically signed and the running app can
reject an unsigned OTA — but `Secure Boot v2` still reports **DISABLED**, because
no eFuse was burned. This proves signing ≠ hardware lock. Fully recoverable:
just rebuild without the options.

---

### Stage 3 — HARDWARE Secure Boot v2 (IRREVERSIBLE) — spare board only

> Do this only on a sacrificial board. Re-read the one-way-door box above.

1. `idf.py menuconfig` → **Security features**:
   - `[*] Enable hardware Secure Boot in bootloader`
   - App Signing Scheme → **ECDSA (v2)**, key length **256**
   - Secure Boot signing key → `secure_boot_signing_key.pem`
   - UART ROM download mode → *Permanently switch to Secure mode* (dev) /
     *Permanently disabled* (production)
   - (Recommended) also enable **Flash Encryption**, mode *Development* for now.

2. Build the signed bootloader and flash it (the build prints the exact
   `esptool write-flash` command — Secure Boot bootloaders are **not** flashed
   by `idf.py flash`):

   ```bash
   idf.py bootloader
   # then run the printed "esptool ... write-flash ... bootloader.bin" command
   ```

3. Flash the signed partition table + app, then watch the **first** boot enable
   Secure Boot (it burns the eFuses during this boot):

   ```bash
   idf.py -p COM15 flash monitor
   ```

   Expected after reset:

   ```
   Secure Boot v2  : ENABLED  (eFuse burned, irreversible)
   ```

4. Verify from the host what got burned:

   ```bash
   idf.py -p COM15 efuse-summary        # look for SECURE_BOOT_EN = 1, a burned BLOCK_KEYx
   ```

**Lesson:** the hardware bit is now set. From here the ROM verifies the
bootloader, the bootloader verifies the app, and any image not signed with
`secure_boot_signing_key.pem` will fail to boot.

---

## Command reference

| Task | Command |
|---|---|
| Set chip target | `idf.py set-target esp32c5` |
| Build | `idf.py build` |
| Flash + monitor | `idf.py -p COM15 flash monitor` |
| Clean re-config | `idf.py fullclean` |
| Generate ECDSA-256 key | `idf.py secure-generate-signing-key --scheme ecdsa256 KEY.pem` |
| Sign a binary manually | `idf.py secure-sign-data --keyfile KEY.pem --output signed.bin in.bin` |
| Verify a signature | `idf.py secure-verify-signature --keyfile KEY.pem signed.bin` |
| Inspect signatures | `espsecure signature-info-v2 build/secure_boot_demo.bin` |
| Read eFuses | `idf.py -p COM15 efuse-summary` |
| Build signed bootloader (Stage 3) | `idf.py bootloader` → then run the printed `esptool … write-flash 0x2000 …` |
| Burn Secure Boot eFuses (manual) | `espefuse -p COM15 --chip esp32c5 burn-key BLOCK_KEY0 digest.bin SECURE_BOOT_DIGEST0` · then `… burn-efuse SECURE_BOOT_EN` |
| Re-flash an encrypted device (dev mode) | `idf.py -p COM15 encrypted-flash monitor` |

> 📇 **Printable one-pager:** [`secure-boot-cheatsheet.html`](./secure-boot-cheatsheet.html) collects every command above (all four stages + recovery) on a single print/PDF-optimized page.

## Files in this project

| File | Role |
|---|---|
| `main/secure_boot_demo_main.c` | Runtime status app — reads eFuses, prints ON/OFF |
| `main/CMakeLists.txt` | Registers the app component |
| `CMakeLists.txt` | Top-level ESP-IDF project file |
| `partitions.csv` | Custom table; table pushed to 0xD000 to fit a signed bootloader |
| `sdkconfig.defaults` | Stage 1 baseline + commented Stage 2/3 option blocks |
| `SECURE_BOOT_DEEP_DIVE.md` | One-stop deep dive — boot flow, chip matrix, schemes, eFuses, per-file map, threat model, glossary |
| `SECURE_BOOT_VISUAL_GUIDE.md` | Practical visual map of code changes, flashing, OTA, Flash Encryption, and key rotation |
| `SECURE_BOOT_IMPLEMENTATION_GUIDE.md` | Step-by-step implementation checklist with commands, OTA layout, code snippets, testing scenarios |
| `secure-boot-flow.html` | Interactive, animated diagram of the boot-time chain of trust (open in a browser) |
| `secure-boot-setup.html` | Interactive hands-on setup lab — staged commands, simulated serial/menuconfig output, eFuse before/after |
| `secure-boot-cheatsheet.html` | One-page printable command quick reference (all stages + recovery) |
| `CLAUDE.md` | Working rules for Claude Code / contributors in this folder |

## Key-safety rules

- **Never commit `secure_boot_signing_key.pem`.** Whoever holds it can forge
  firmware for every device that trusts it. (This project's `.gitignore`
  excludes `*.pem`.)
- Back the key up somewhere offline; losing it means no more updates for locked
  devices.
- For production, generate the key on a trusted machine (or HSM), not on the
  build server.
