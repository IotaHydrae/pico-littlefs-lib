cmake_minimum_required(VERSION 3.13)

# ---- Top-level project ---------------------------------------------------
# Include the Pico SDK (bootstrap if needed)
include(${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib/pico_sdk_import.cmake)

project(pico-littlefs-lib C CXX)
pico_sdk_init()

# ---- pico_sdcard library -------------------------------------------------
# The SD card library is platform-independent; the port lives alongside it.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib sdcard_lib_build)

# ---- littlefs library ----------------------------------------------------
# littlefs is pulled in as source files directly.
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

# ---- block device abstraction & SD card backend --------------------------
add_library(pico_littlefs_bd STATIC
    ${CMAKE_CURRENT_LIST_DIR}/lfs_bd_sdcard.c
)
target_include_directories(pico_littlefs_bd PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib/littlefs
    ${CMAKE_CURRENT_LIST_DIR}/lib/sdcard-lib
)
target_link_libraries(pico_littlefs_bd PUBLIC
    pico_sdcard
    pico_littlefs_core
)

# ---- example -------------------------------------------------------------
add_subdirectory(example)
