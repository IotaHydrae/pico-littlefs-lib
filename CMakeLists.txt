cmake_minimum_required(VERSION 3.13)

# =========================================================================
# pico-littlefs — CMake build
#
# Three scenarios are handled:
#
#   A. TOP-LEVEL PROJECT
#      Bootstrap the Pico SDK and build the example.
#
#   B. SUBMODULE in a Pico SDK project
#      The parent already called pico_sdk_init().  Library targets
#      (pico_littlefs_core, pico_littlefs_bd) are defined.  The
#      example is NOT built — the parent provides its own main().
#
#   C. SUBMODULE in a non-Pico project
#      Only pico_littlefs_core (pure littlefs, zero deps) is built.
#
# Usage from a parent Pico project:
#
#     add_subdirectory(path/to/pico-littlefs-lib)
#     target_link_libraries(your_app pico_littlefs_bd pico_sdcard ...)
# =========================================================================

# ---- scenario A: bootstrap Pico SDK -----------------------------------
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib/pico_sdk_import.cmake)
    project(pico-littlefs-lib C CXX)
    pico_sdk_init()
endif()

# ---- library: littlefs core (always built, zero Pico SDK dependency) ----
if(NOT TARGET pico_littlefs_core)
    add_library(pico_littlefs_core STATIC
        ${CMAKE_CURRENT_LIST_DIR}/lib/littlefs/lfs.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/littlefs/lfs_util.c
    )
    target_include_directories(pico_littlefs_core PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/lib/littlefs
    )
    target_compile_definitions(pico_littlefs_core PRIVATE
        LFS_NO_DEBUG
        LFS_NO_WARN
        LFS_NO_ERROR
    )
endif()

# ---- library: sdcard (from submodule, guarded against double-include) --
if(NOT TARGET pico_sdcard)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib sdcard_lib_build)
endif()

# ---- library: spi-flash (from submodule, guarded against double-include) --
if(NOT TARGET pico_spi_flash)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/spi-flash-lib spi_flash_lib_build)
endif()

# ---- library: block-device backends (SD + flash) -----------------------
if(NOT TARGET pico_littlefs_bd)
    add_library(pico_littlefs_bd STATIC
        ${CMAKE_CURRENT_LIST_DIR}/lfs_bd_sdcard.c
        ${CMAKE_CURRENT_LIST_DIR}/lfs_bd_flash.c
    )
    target_include_directories(pico_littlefs_bd PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/lib/littlefs
        ${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib
        ${CMAKE_CURRENT_LIST_DIR}/lib/spi-flash-lib
    )
    target_link_libraries(pico_littlefs_bd PUBLIC
        pico_sdcard
        pico_spi_flash
        pico_littlefs_core
    )
endif()

# ---- example (only when this IS the top-level project) ------------------
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # ---- Scenario A ------------------------------------------------------
    add_subdirectory(example)

elseif(TARGET pico_stdlib)
    # ---- Scenario B: submodule in a Pico project -------------------------
    # Libraries are already defined above.  The parent project provides
    # sdcard_port_pico.c and its own main().
    #
    # The example is intentionally NOT built here.

endif()
# else: Scenario C — only pico_littlefs_core is available.
