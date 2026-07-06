# Top-level ESP-IDF project file. Do NOT put application source here — the app
# lives in main/. `idf.py` reads this to discover the project.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(secure_boot_demo)
