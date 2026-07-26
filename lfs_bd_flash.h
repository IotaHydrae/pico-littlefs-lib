/*
 * lfs_bd_flash — LittleFS Block Device for SPI NOR Flash
 *
 * Maps littlefs read / prog / erase / sync onto spi_flash_read() /
 * spi_flash_write() / spi_flash_erase_sector() calls.
 *
 * ## Typical usage
 *
 *     #include "lfs_bd_flash.h"
 *     #include "lfs.h"
 *
 *     static lfs_bd_flash_t bd;
 *     static struct lfs_config cfg;
 *     static lfs_t lfs;
 *
 *     spi_flash_init();
 *     lfs_bd_flash_init(&cfg, &bd);
 *     lfs_format(&lfs, &cfg);
 *     lfs_mount(&lfs, &cfg);
 *
 * ## Geometry
 *
 *   read_size  = 1     (byte-level reads)
 *   prog_size  = 1     (flash writes handle byte alignment)
 *   block_size = 4096  (NOR flash sector erase size)
 *
 *   cache_size defaults to block_size.
 *
 * ## Flash erase
 *
 * Unlike SD cards, NOR flash REQUIRES explicit erase-before-write.
 * The erase callback calls spi_flash_erase_sector(), which
 * erases a 4 KiB sector.  This is a blocking operation (~50 ms).
 */

#ifndef LFS_BD_FLASH_H
#define LFS_BD_FLASH_H

#include "lfs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Default geometry (override via -D compiler flags)
 * -------------------------------------------------------------------------- */

#ifndef LFS_BD_FLASH_DEFAULT_BLOCK_SIZE
#define LFS_BD_FLASH_DEFAULT_BLOCK_SIZE 4096
#endif

#ifndef LFS_BD_FLASH_DEFAULT_CACHE_SIZE
#define LFS_BD_FLASH_DEFAULT_CACHE_SIZE 4096
#endif

/* --------------------------------------------------------------------------
 * Backend instance
 * -------------------------------------------------------------------------- */

typedef struct lfs_bd_flash {
	/* Total bytes available for the filesystem.
	 * Set to 0 to auto-detect via spi_flash_get_info(). */
	uint32_t size_limit;

	/* Starting byte offset on the flash chip.
	 * Usually 0 (use entire chip). */
	uint32_t byte_offset;
} lfs_bd_flash_t;

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

/**
 * Populate *cfg so it is ready for lfs_format() or lfs_mount().
 *
 * @param cfg  Pointer to caller-owned lfs_config; all fields are written.
 * @param bd   Pointer to caller-owned lfs_bd_flash_t; stored in cfg->context.
 * @return LFS_ERR_OK on success.
 */
int lfs_bd_flash_init(struct lfs_config *cfg, lfs_bd_flash_t *bd);

#ifdef __cplusplus
}
#endif

#endif /* LFS_BD_FLASH_H */
