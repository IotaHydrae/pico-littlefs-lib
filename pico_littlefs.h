/*
 * pico_littlefs — Convenience Wrapper
 *
 * Common boilerplate for working with littlefs on the Raspberry Pi Pico.
 * This is a thin, optional layer — you can also use lfs_format() /
 * lfs_mount() directly with any lfs_config you populate yourself.
 *
 * ## Usage
 *
 *     #include "lfs_bd_sdcard.h"
 *     #include "pico_littlefs.h"
 *
 *     static lfs_bd_sdcard_t bd;
 *     static struct lfs_config cfg;
 *     static lfs_t lfs;
 *
 *     sd_init();
 *     lfs_bd_sdcard_init(&cfg, &bd);
 *
 *     // Format (once, before first mount)
 *     pico_littlefs_format(&lfs, &cfg);
 *
 *     // Mount
 *     int err = pico_littlefs_mount(&lfs, &cfg);
 *
 *     // ... use lfs_file_open / lfs_file_read / lfs_file_write / ...
 *
 *     pico_littlefs_unmount(&lfs);
 */

#ifndef PICO_LITTLEFS_H
#define PICO_LITTLEFS_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format the block device with littlefs.
 *
 * WARNING: destroys all existing data on the device.
 *
 * @return LFS_ERR_OK on success, negative error code otherwise.
 */
int pico_littlefs_format(lfs_t *lfs, const struct lfs_config *cfg);

/**
 * Mount an existing littlefs filesystem.
 *
 * @return LFS_ERR_OK on success, negative error code otherwise.
 *         LFS_ERR_CORRUPT may indicate that the device needs formatting.
 */
int pico_littlefs_mount(lfs_t *lfs, const struct lfs_config *cfg);

/**
 * Unmount the filesystem.
 *
 * Flushes all internal caches and releases resources.  Safe to call
 * even if the filesystem was never mounted.
 *
 * @return LFS_ERR_OK on success.
 */
int pico_littlefs_unmount(lfs_t *lfs);

#ifdef __cplusplus
}
#endif

#endif /* PICO_LITTLEFS_H */
