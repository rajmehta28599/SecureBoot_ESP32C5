# Repository Guidelines

## Project Structure & Module Organization

This is an ESP-IDF project for the ESP32-C5 Secure Boot v2 demo. The root `CMakeLists.txt` defines the project and delegates application code to `main/`. Runtime code lives in `main/secure_boot_demo_main.c`, with component registration in `main/CMakeLists.txt`. Build configuration is seeded by `sdkconfig.defaults`; the generated `sdkconfig` reflects the active local configuration. `partitions.csv` defines the flash layout. Generated artifacts belong in `build/` and should not be edited by hand. Project documentation lives in `README.md`, `SECURE_BOOT_DEEP_DIVE.md`, and the secure boot HTML guides.

## Build, Test, and Development Commands

- `idf.py set-target esp32c5`: configure the target chip once for this checkout.
- `idf.py build`: compile the bootloader, partition table, and app.
- `idf.py -p COM15 flash monitor`: flash the app and open the serial monitor. Adjust the port for your machine.
- `idf.py fullclean`: clear generated CMake and build state after configuration changes.
- `idf.py menuconfig`: edit ESP-IDF configuration safely through Kconfig.
- `idf.py -p COM15 efuse-summary`: inspect eFuse state without changing it.

No automated test suite is currently present. For now, verification is by successful `idf.py build` plus serial monitor output from the demo app.

## Coding Style & Naming Conventions

Use C style consistent with the existing source: 4-space indentation, `snake_case` functions and variables, `static` helpers for file-local functions, and uppercase log tags such as `SB_DEMO`. Keep comments useful and focused on hardware or security behavior. Do not put application source in the root; add new source files under `main/` and register them in `main/CMakeLists.txt`.

## Security & Configuration Tips

Treat Secure Boot and eFuse operations as irreversible. Do not run `idf.py efuse-burn`, `efuse-burn-key`, or flash a `CONFIG_SECURE_BOOT=y` bootloader without explicit board-aware approval. Keep private signing keys out of the repository; `*.pem` and secure boot key names should remain ignored. Prefer `idf.py menuconfig` over hand-editing secure boot options.

## Commit & Pull Request Guidelines

Git history uses short, imperative commit messages such as `Add command quick reference...` or `Refactor code structure...`. Keep commits focused. Pull requests should explain the behavior changed, list build or hardware checks run, and call out any Secure Boot, Flash Encryption, eFuse, partition, or signing-key impact.
