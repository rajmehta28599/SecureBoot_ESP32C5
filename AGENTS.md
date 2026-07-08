# Repository Guidelines

## Project Structure & Module Organization

This is an ESP-IDF project for the ESP32-C5 Secure Boot v2 demo. The root `CMakeLists.txt` defines the project and delegates application code to `main/`. Runtime code lives in `main/secure_boot_demo_main.c`; add new sources under `main/` and register them in `main/CMakeLists.txt`. `sdkconfig.defaults` seeds configuration, and `partitions.csv` defines flash layout. Do not edit generated files in `build/`.

## Build, Test, and Development Commands

- `idf.py set-target esp32c5`: set the target chip once.
- `idf.py build`: compile bootloader, partition table, and app.
- `idf.py -p COM15 flash monitor`: flash over USB/UART and open logs.
- `idf.py fullclean`: clear build state after config changes.
- `idf.py menuconfig`: edit ESP-IDF configuration through Kconfig.
- `idf.py -p COM15 efuse-summary`: inspect eFuses without changing them.

No automated test suite is present. Verify with `idf.py build` and serial monitor output.

## Code Change & Reupload Workflow

Make firmware changes in `main/`, then run `idf.py build`. Before hardware Secure Boot is enabled, reupload over USB/UART with `idf.py -p COM15 flash monitor`. After Secure Boot is enabled, app images must be signed by the trusted private key or the bootloader rejects them. Avoid reflashing the bootloader unless intentionally provisioning a board; it can burn irreversible eFuses.

This repo has only a `factory` app partition, so OTA is not enabled. OTA needs `otadata`, `ota_0`/`ota_1`, update code such as `esp_https_ota`, rollback handling, and signed app images. Use USB/UART for lab work; use OTA for production or inaccessible devices. With Flash Encryption, use ESP-IDF encrypted flashing and confirm the device remains in reflashable development mode before relying on UART updates.

## Coding Style & Naming Conventions

Use the existing C style: 4-space indentation, `snake_case` names, `static` file-local helpers, and uppercase log tags such as `SB_DEMO`. Keep comments focused on hardware or security behavior.

## Security & Configuration Tips

Treat Secure Boot and eFuse operations as irreversible. Do not run `idf.py efuse-burn`, `efuse-burn-key`, or flash a `CONFIG_SECURE_BOOT=y` bootloader without explicit board-aware approval. Keep private signing keys out of the repository. Prefer `idf.py menuconfig` over hand-editing secure boot options.

## Commit & Pull Request Guidelines

Git history uses short, imperative commit messages such as `Add command quick reference...` or `Refactor code structure...`. Keep commits focused. Pull requests should explain the behavior changed, list build or hardware checks run, and call out any Secure Boot, Flash Encryption, eFuse, partition, or signing-key impact.
