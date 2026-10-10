cmake_minimum_required(VERSION 3.16)

set(EPAPER_SRCS
    "main.c" "epd_drv.c" "settings.c" "netif_ap.c" "captive.c"
    "ota_web.c" "frame.c" "power.c" "store.c" "wifi_lan.c")

# Regenerate the embedded page on configuration, including after page/version edits.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../page/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_index_header.py")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/make_index_header.py"
    RESULT_VARIABLE EPAPER_PAGE_RESULT)
if(NOT EPAPER_PAGE_RESULT EQUAL 0)
    message(FATAL_ERROR "Stillink embedded page generation failed")
endif()

idf_component_register(
    SRCS ${EPAPER_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi nvs_flash esp_wifi esp_netif esp_event
                  esp_http_server esp_adc esp_hw_support
                  app_update spi_flash esp_partition esp_timer lwip
)

# 显式声明页面头和版本头的编译依赖。
set_property(SOURCE main.c APPEND PROPERTY OBJECT_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/index_html.h")
set_property(SOURCE ${EPAPER_SRCS} APPEND PROPERTY OBJECT_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/version.h")

# R1.2.0（FB-015）：控制页一律内嵌（唯一真源 = page/index.html），
# 原有的 EPAPER_EMBED_PAGE 逃生开关与占位页已删除 —— 设备必须自带控制页，
# 否则首次连接无页面可用；「固件放不下」也早有实际余量（app 仅占槽位 6 成）。
