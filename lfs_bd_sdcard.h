/*
 * lfs_bd_sdcard — LittleFS Block Device for SPI SD Cards
 *
 * Maps littlefs read / prog / erase / sync onto sd_read_block() /
 * sd_write_block() calls.
 *
 * ## Typical usage
 *
 *     #include "lfs_bd_sdcard.h"
 *     #include "lfs.h"
 *
 *     static lfs_bd_sdcard_t bd;
 *     static struct lfs_config cfg;
 *     static lfs_t lfs;
 *
 *     // Initialise SD card hardware first
 *     int err = sd_init();
 *
 *     // Wire up littlefs → SD card
 *     err = lfs_bd_sdcard_init(&cfg, &bd);
 *
 *     // Format (first use only)
 *     err = lfs_format(&lfs, &cfg);
 *
 *     // Mount
 *     err = lfs_mount(&lfs, &cfg);
 *
 * ## Geometry
 *
 *   read_size  = 512   (SD sector size)
 *   prog_size  = 512
 *   block_size = 4096  (8 sectors — better metadata performance and
 *                        lower lookahead pressure on large cards;
 *                        set to 512 via cfg after init if you need
 *                        cross-compatibility with PC littlefs-fuse
 *                        without command-line flags)
 *
 *   cache_size defaults to block_size.  Override after init but
 *   before lfs_format() / lfs_mount().
 *
 * ## PC cross-mounting
 *
 *   Pico → PC:  ./lfs --block_size=4096 /dev/sda mount
 *   PC  → Pico: lfs_bd_sdcard_mount_auto() detects block_size=512
 *               automatically (formatted with ./lfs --format /dev/sda)
 *
 * ## SD-card erase
 *
 * SD cards do not need explicit erase-before-write, so the erase
 * callback is a no-op.  This is fine for littlefs — it just means
 * wear-levelling decisions are made solely by the card's internal
 * controller.
 */

#ifndef LFS_BD_SDCARD_H
#define LFS_BD_SDCARD_H

#include "lfs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Backend instance
 * -------------------------------------------------------------------------- */

typedef struct lfs_bd_sdcard {
    /* Total number of SD sectors available for the filesystem.
     * Set to 0 to auto-detect via CSD (sd_get_sector_count).
     * Set non-zero to override / clamp the capacity. */
    uint32_t sector_count_limit;

    /* Starting sector offset on the card.
     * Usually 0 (use entire card).  Set to a non-zero value to place
     * the filesystem after an MBR / partition table. */
    uint32_t sector_offset;
} lfs_bd_sdcard_t;

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

/**
 * Populate *cfg so it is ready for lfs_format() or lfs_mount().
 *
 * @param cfg  Pointer to caller-owned lfs_config; all fields are written.
 * @param bd   Pointer to caller-owned lfs_bd_sdcard_t; stored in cfg->context.
 * @return LFS_ERR_OK on success.
 *
 * After this call you may tweak cfg->block_size, cfg->cache_size,
 * cfg->lookahead_size, cfg->read_buffer, cfg->prog_buffer, or
 * cfg->lookahead_buffer before formatting / mounting.
 * lfs_bd_sdcard_init exports the default 4 KiB block geometry.
 */
int lfs_bd_sdcard_init(struct lfs_config *cfg, lfs_bd_sdcard_t *bd);

/**
 * Mount with automatic geometry detection.
 *
 * Tries lfs_mount() with a sequence of common block sizes (4096, 512,
 * …).  The first size that mounts successfully is kept in cfg.
 *
 * This allows the same firmware to mount filesystems formatted by the
 * PC littlefs-fuse tool (block_size = 512) as well as those formatted
 * by this library (block_size = 4096).
 *
 * cfg must already be initialised by lfs_bd_sdcard_init().  Static
 * buffers (read_buffer / prog_buffer) must be sized for the largest
 * block size the caller wants to support.
 *
 * @return LFS_ERR_OK on success.
 *         LFS_ERR_INVAL if no probed geometry matched.
 *         Other negative codes are passed through from lfs_mount().
 */
int lfs_bd_sdcard_mount_auto(lfs_t *lfs, struct lfs_config *cfg,
                             lfs_bd_sdcard_t *bd);

#ifdef __cplusplus
}
#endif

#endif /* LFS_BD_SDCARD_H */
