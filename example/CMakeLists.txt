cmake_minimum_required(VERSION 3.13)

# ---- common source & link settings --------------------------------------
set(BD_PORT_SRC ${CMAKE_CURRENT_LIST_DIR}/../sdcard_port_pico.c)

set(COMMON_LIBS
    pico_sdcard
    pico_littlefs_bd
    pico_stdlib
    hardware_spi
    hardware_gpio
)

set(COMMON_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/../lib/sdcard-lib
)

# ---- pico-littlefs-demo (full-featured demo) ----------------------------
add_executable(pico-littlefs-demo
    main.c
    ${BD_PORT_SRC}
)

target_link_libraries(pico-littlefs-demo ${COMMON_LIBS})
target_include_directories(pico-littlefs-demo PRIVATE ${COMMON_INCLUDES})

pico_enable_stdio_usb(pico-littlefs-demo 1)
pico_enable_stdio_uart(pico-littlefs-demo 1)
pico_add_extra_outputs(pico-littlefs-demo)

# ---- pico-large-read (large file read benchmark) ------------------------
add_executable(pico-large-read
    large_read.c
    ${BD_PORT_SRC}
)

target_link_libraries(pico-large-read ${COMMON_LIBS})
target_include_directories(pico-large-read PRIVATE ${COMMON_INCLUDES})

pico_enable_stdio_usb(pico-large-read 1)
pico_enable_stdio_uart(pico-large-read 1)
pico_add_extra_outputs(pico-large-read)
