# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS "../main/main.c" 
                        INCLUDE_DIRS "."
                        REQUIRES esp_http_client json esp_wifi nvs_flash
                        PRIV_REQUIRES esp_timer driver)