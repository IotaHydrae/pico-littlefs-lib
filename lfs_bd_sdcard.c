/*
 * lfs_bd_sdcard — LittleFS Block Device for SPI SD Cards
 *
 * Implementation: translates littlefs block-level I/O into SD-card
 * sector reads and writes via the platform-independent sdcard library.
 */

#include "lfs_bd_sdcard.h"
#include "sdcard.h"

#include <string.h>

/* =========================================================================
 * Default geometry
 * ========================================================================= */

#ifndef LFS_BD_SDCARD_BLOCK_SIZE
#define LFS_BD_SDCARD_BLOCK_SIZE 4096
#endif

#ifndef LFS_BD_SDCARD_CACHE_SIZE
#define LFS_BD_SDCARD_CACHE_SIZE 4096
#endif

/* =========================================================================
 * Helpers — convert littlefs (block, off) → SD sector address
 * ========================================================================= */

static inline uint32_t lfs_block_to_sd_sector(const lfs_bd_sdcard_t *bd,
                                               lfs_block_t block,
                                               lfs_off_t off)
{
    /* block and off are in littlefs space (block_size granularity).
     * Convert to absolute byte address then to SD-sector index. */
    uint32_t byte_addr = (uint32_t)(block * LFS_BD_SDCARD_BLOCK_SIZE + off);
    return bd->sector_offset + (byte_addr / 512);
}

/* =========================================================================
 * lfs_config callbacks
 * ========================================================================= */

static int bd_sdcard_read(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size)
{
    lfs_bd_sdcard_t *bd = (lfs_bd_sdcard_t *)c->context;
    uint32_t sector = lfs_block_to_sd_sector(bd, block, off);
    uint32_t sectors = size / 512;
    uint8_t *dst = (uint8_t *)buffer;

    for (uint32_t i = 0; i < sectors; i++) {
        int err = sd_read_block(sector + i, dst + i * 512);
        if (err != SD_OK) return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

static int bd_sdcard_prog(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size)
{
    lfs_bd_sdcard_t *bd = (lfs_bd_sdcard_t *)c->context;
    uint32_t sector = lfs_block_to_sd_sector(bd, block, off);
    uint32_t sectors = size / 512;
    const uint8_t *src = (const uint8_t *)buffer;

    for (uint32_t i = 0; i < sectors; i++) {
        int err = sd_write_block(sector + i, src + i * 512);
        if (err != SD_OK) return LFS_ERR_IO;
    }

    return LFS_ERR_OK;
}

static int bd_sdcard_erase(const struct lfs_config *c, lfs_block_t block)
{
    /* SD cards manage erase internally — explicit erase is unnecessary
     * for correct operation.  littlefs calls this for wear-levelling
     * hints; we accept it silently. */
    (void)c;
    (void)block;
    return LFS_ERR_OK;
}

static int bd_sdcard_sync(const struct lfs_config *c)
{
    /* SD card writes are synchronous — nothing to flush. */
    (void)c;
    return LFS_ERR_OK;
}

/* =========================================================================
 * Init
 * ========================================================================= */

int lfs_bd_sdcard_init(struct lfs_config *cfg, lfs_bd_sdcard_t *bd)
{
    if (!cfg || !bd) return LFS_ERR_INVAL;

    memset(cfg, 0, sizeof(*cfg));

    /* ---- geometry --------------------------------------------------- */
    cfg->read_size  = 512;
    cfg->prog_size  = 512;
    cfg->block_size = LFS_BD_SDCARD_BLOCK_SIZE;

    /* Default block_count: query the card for its real capacity.
     * If sector_count_limit is non-zero it overrides the detected value.
     * Falls back to a conservative maximum if the card can't be queried. */
    uint32_t total_sectors = bd->sector_count_limit;
    if (total_sectors == 0) {
        total_sectors = sd_get_sector_count();
        if (total_sectors == 0)
            total_sectors = 0xFFFFFFFF;  /* last-resort fallback */
    }
    uint32_t usable_sectors = total_sectors - bd->sector_offset;
    cfg->block_count = usable_sectors / (LFS_BD_SDCARD_BLOCK_SIZE / 512);

    cfg->block_cycles = 500;
    cfg->cache_size   = LFS_BD_SDCARD_CACHE_SIZE;
    cfg->lookahead_size = 128;   /* track 1024 blocks at a time */

    /* ---- callbacks -------------------------------------------------- */
    cfg->context = bd;
    cfg->read    = bd_sdcard_read;
    cfg->prog    = bd_sdcard_prog;
    cfg->erase   = bd_sdcard_erase;
    cfg->sync    = bd_sdcard_sync;

    /* caller is responsible for optional static buffers:
     *   cfg->read_buffer
     *   cfg->prog_buffer
     *   cfg->lookahead_buffer
     */

    return LFS_ERR_OK;
}
