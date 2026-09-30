# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       PRIV_REQUIRES esp_driver_sdmmc sdmmc fatfs mbedtls lwip esp_wifi esp_netif nvs_flash esp_http_server)
