cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(IDF_TARGET esp32c6)
set(SECURE_BOOT_SIGNING_KEY "secure_boot_signing_key.pem")
project(Gate-System)
