# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS "../main/main.c" 
                        INCLUDE_DIRS "."
                        REQUIRES esp_wifi mqtt nvs_flash esp_event
                        PRIV_REQUIRES esp_timer driver)