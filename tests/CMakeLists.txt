cmake_minimum_required(VERSION 3.13)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

set(FLASH_PORT_SRC ${ROOT}/lib/spi-flash-lib/examples/pico/flash_port_pico.c)
set(SD_PORT_SRC     ${ROOT}/sdcard_port_pico.c)

set(COMMON_SRC stress_common.c)

set(FLASH_LIBS pico_spi_flash pico_littlefs_bd pico_stdlib hardware_spi hardware_gpio hardware_dma)
set(SD_LIBS    pico_sdcard    pico_littlefs_bd pico_stdlib hardware_spi hardware_gpio)

set(FLASH_INCLUDES ${ROOT} ${ROOT}/lib/littlefs ${ROOT}/lib/spi-flash-lib)
set(SD_INCLUDES    ${ROOT} ${ROOT}/lib/littlefs ${ROOT}/lib/sdcard-lib)

# ---- pico-flash-stress ---------------------------------------------------
add_executable(pico-flash-stress
    ${COMMON_SRC}
    flash_stress.c
    ${FLASH_PORT_SRC}
)
target_link_libraries(pico-flash-stress ${FLASH_LIBS})
target_include_directories(pico-flash-stress PRIVATE ${FLASH_INCLUDES})
pico_enable_stdio_usb(pico-flash-stress 1)
pico_enable_stdio_uart(pico-flash-stress 1)
pico_add_extra_outputs(pico-flash-stress)

# ---- pico-sdcard-stress --------------------------------------------------
add_executable(pico-sdcard-stress
    ${COMMON_SRC}
    sdcard_stress.c
    ${SD_PORT_SRC}
)
target_link_libraries(pico-sdcard-stress ${SD_LIBS})
target_include_directories(pico-sdcard-stress PRIVATE ${SD_INCLUDES})
pico_enable_stdio_usb(pico-sdcard-stress 1)
pico_enable_stdio_uart(pico-sdcard-stress 1)
pico_add_extra_outputs(pico-sdcard-stress)

# ---- Run-time parameters (override via cmake -D) -------------------------
foreach(TARGET pico-flash-stress pico-sdcard-stress)
    if(DEFINED STRESS_DURATION_HOURS)
        target_compile_definitions(${TARGET} PRIVATE
            DURATION_HOURS=${STRESS_DURATION_HOURS})
    endif()
    if(DEFINED STRESS_STATUS_INTERVAL_MIN)
        target_compile_definitions(${TARGET} PRIVATE
            STATUS_INTERVAL_MIN=${STRESS_STATUS_INTERVAL_MIN})
    endif()
endforeach()
