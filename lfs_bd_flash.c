/*
 * lfs_bd_flash — LittleFS Block Device for SPI NOR Flash
 *
 * Implementation: translates littlefs block-level I/O into
 * byte-level flash read / write / erase calls.
 */

#include "lfs_bd_flash.h"
#include "flash.h"

#include <string.h>

/* =========================================================================
 * lfs_config callbacks
 * ========================================================================= */

static int bd_flash_read(const struct lfs_config *c, lfs_block_t block,
			 lfs_off_t off, void *buffer, lfs_size_t size)
{
	lfs_bd_flash_t *bd = (lfs_bd_flash_t *)c->context;

	uint32_t addr = bd->byte_offset +
			(uint32_t)((uint64_t)block * c->block_size + off);
	int err = spi_flash_read(addr, (uint8_t *)buffer, size);
	if (err != SPI_FLASH_OK)
		return LFS_ERR_IO;
	return LFS_ERR_OK;
}

static int bd_flash_prog(const struct lfs_config *c, lfs_block_t block,
			 lfs_off_t off, const void *buffer, lfs_size_t size)
{
	lfs_bd_flash_t *bd = (lfs_bd_flash_t *)c->context;

	uint32_t addr = bd->byte_offset +
			(uint32_t)((uint64_t)block * c->block_size + off);
	int err = spi_flash_write(addr, (const uint8_t *)buffer, size);
	if (err != SPI_FLASH_OK)
		return LFS_ERR_IO;
	return LFS_ERR_OK;
}

static int bd_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
	lfs_bd_flash_t *bd = (lfs_bd_flash_t *)c->context;

	/* NOR flash requires explicit erase-before-write.  Each littlefs
	 * block maps to one 4 KiB flash sector. */
	uint32_t addr = bd->byte_offset +
			(uint32_t)((uint64_t)block * c->block_size);
	int err = spi_flash_erase_sector(addr);
	if (err != SPI_FLASH_OK)
		return LFS_ERR_IO;
	return LFS_ERR_OK;
}

static int bd_flash_sync(const struct lfs_config *c)
{
	/* spi_flash_write() already waits for the chip's write-in-progress
	 * status after each page program.  Nothing to flush. */
	(void)c;
	return LFS_ERR_OK;
}

/* =========================================================================
 * Init
 * ========================================================================= */

int lfs_bd_flash_init(struct lfs_config *cfg, lfs_bd_flash_t *bd)
{
	if (!cfg || !bd)
		return LFS_ERR_INVAL;

	memset(cfg, 0, sizeof(*cfg));

	/* ---- geometry --------------------------------------------------- */
	cfg->read_size = 1; /* byte-level reads */
	cfg->prog_size = 1; /* flash handles byte alignment */
	cfg->block_size = LFS_BD_FLASH_DEFAULT_BLOCK_SIZE;

	/* Auto-detect flash size if not specified */
	uint32_t total_bytes = bd->size_limit;
	if (total_bytes == 0) {
		spi_flash_info_t info;
		if (spi_flash_get_info(&info) == SPI_FLASH_OK)
			total_bytes = info.size_bytes;
		if (total_bytes == 0)
			total_bytes = 0xFFFFFFFF;
	}
	uint32_t usable_bytes = total_bytes - bd->byte_offset;
	cfg->block_count = usable_bytes / cfg->block_size;

	cfg->block_cycles = 500;
	cfg->cache_size = LFS_BD_FLASH_DEFAULT_CACHE_SIZE;
	cfg->lookahead_size = 256;

	/* ---- callbacks -------------------------------------------------- */
	cfg->context = bd;
	cfg->read = bd_flash_read;
	cfg->prog = bd_flash_prog;
	cfg->erase = bd_flash_erase;
	cfg->sync = bd_flash_sync;

	return LFS_ERR_OK;
}
