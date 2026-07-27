cmake_minimum_required(VERSION 3.13)

set(FLASH_PORT_SRC ${CMAKE_CURRENT_LIST_DIR}/../lib/spi-flash-lib/examples/pico/flash_port_pico.c)

set(FLASH_LIBS
    pico_spi_flash
    pico_littlefs_bd
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_dma
)

set(FLASH_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../lib/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/../lib/spi-flash-lib
)

# ---- pico-flash-stress (SPI flash stability test) ----------------------
add_executable(pico-flash-stress
    stress_common.c
    flash_stress.c
    ${FLASH_PORT_SRC}
)

target_link_libraries(pico-flash-stress ${FLASH_LIBS})
target_include_directories(pico-flash-stress PRIVATE ${FLASH_INCLUDES})

# Run-time parameters (override via cmake -D)
if(DEFINED STRESS_DURATION_HOURS)
    target_compile_definitions(pico-flash-stress PRIVATE
        DURATION_HOURS=${STRESS_DURATION_HOURS})
endif()
if(DEFINED STRESS_STATUS_INTERVAL_MIN)
    target_compile_definitions(pico-flash-stress PRIVATE
        STATUS_INTERVAL_MIN=${STRESS_STATUS_INTERVAL_MIN})
endif()

pico_enable_stdio_usb(pico-flash-stress 1)
pico_enable_stdio_uart(pico-flash-stress 1)
pico_add_extra_outputs(pico-flash-stress)
