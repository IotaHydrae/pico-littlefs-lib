cmake_minimum_required(VERSION 3.13)

add_executable(pico-littlefs-demo
    main.c
    ${CMAKE_SOURCE_DIR}/sdcard_port_pico.c
)

target_link_libraries(pico-littlefs-demo
    pico_sdcard
    pico_littlefs_bd
    pico_stdlib
    hardware_spi
    hardware_gpio
)

target_include_directories(pico-littlefs-demo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/littlefs
    ${CMAKE_SOURCE_DIR}/lib/sdcard-lib
)

pico_enable_stdio_usb(pico-littlefs-demo 1)
pico_enable_stdio_uart(pico-littlefs-demo 1)
pico_add_extra_outputs(pico-littlefs-demo)
