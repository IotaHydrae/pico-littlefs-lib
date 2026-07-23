/*
 * pico-littlefs — Block Device Abstraction
 *
 * Each storage backend (SD card, SPI flash, …) provides an init function
 * that fills out a `struct lfs_config` with the appropriate callbacks and
 * geometry.  The rest of the application works with `lfs_t` directly and
 * never cares which backend is underneath.
 *
 * To add a new backend:
 *   1. Create lfs_bd_<name>.h / lfs_bd_<name>.c
 *   2. Provide an init function that populates `struct lfs_config`
 *   3. Implement lfs_config::{read, prog, erase, sync} using your hardware
 */

#ifndef LFS_BD_H
#define LFS_BD_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for any block-device backend.
 * The caller owns the storage; the backend init functions cast through
 * lfs_config.context. */
typedef struct lfs_bd {
    /* Backend-specific data — each implementation defines its own struct
     * and stores a pointer in lfs_config.context. */
    void *impl;
} lfs_bd_t;

#ifdef __cplusplus
}
#endif

#endif /* LFS_BD_H */
