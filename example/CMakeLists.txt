cmake_minimum_required(VERSION 3.13)

# ---- common source & link settings --------------------------------------
set(SD_PORT_SRC  ${CMAKE_CURRENT_LIST_DIR}/../sdcard_port_pico.c)
set(FLASH_PORT_SRC ${CMAKE_CURRENT_LIST_DIR}/../lib/spi-flash-lib/examples/pico/flash_port_pico.c)

set(SD_LIBS
    pico_sdcard
    pico_littlefs_bd
    pico_stdlib
    hardware_spi
    hardware_gpio
)

set(FLASH_LIBS
    pico_spi_flash
    pico_littlefs_bd
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_dma
)

set(SD_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/../lib/sdcard-lib
)

set(FLASH_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/../lib/spi-flash-lib
)

# ---- pico-littlefs-sdcard-demo (SD card demo) ----------------------------------
add_executable(pico-littlefs-sdcard-demo
    sdcard_demo.c
    ${SD_PORT_SRC}
)

target_link_libraries(pico-littlefs-sdcard-demo ${SD_LIBS})
target_include_directories(pico-littlefs-sdcard-demo PRIVATE ${SD_INCLUDES})

pico_enable_stdio_usb(pico-littlefs-sdcard-demo 1)
pico_enable_stdio_uart(pico-littlefs-sdcard-demo 1)
pico_add_extra_outputs(pico-littlefs-sdcard-demo)

# ---- pico-littlefs-sdcard-large-read (SD card large file benchmark) ---------------------
add_executable(pico-littlefs-sdcard-large-read
    sdcard_large_read.c
    ${SD_PORT_SRC}
)

target_link_libraries(pico-littlefs-sdcard-large-read ${SD_LIBS})
target_include_directories(pico-littlefs-sdcard-large-read PRIVATE ${SD_INCLUDES})

pico_enable_stdio_usb(pico-littlefs-sdcard-large-read 1)
pico_enable_stdio_uart(pico-littlefs-sdcard-large-read 1)
pico_add_extra_outputs(pico-littlefs-sdcard-large-read)

# ---- pico-littlefs-flash-demo (SPI flash demo) -----------------------------------
add_executable(pico-littlefs-flash-demo
    flash_demo.c
    ${FLASH_PORT_SRC}
)

target_link_libraries(pico-littlefs-flash-demo ${FLASH_LIBS})
target_include_directories(pico-littlefs-flash-demo PRIVATE ${FLASH_INCLUDES})

pico_enable_stdio_usb(pico-littlefs-flash-demo 1)
pico_enable_stdio_uart(pico-littlefs-flash-demo 1)
pico_add_extra_outputs(pico-littlefs-flash-demo)
