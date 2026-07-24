/*
 * lfs_bd_sdcard — LittleFS Block Device for SPI SD Cards
 *
 * Implementation: translates littlefs block-level I/O into SD-card
 * sector reads and writes via the platform-independent sdcard library.
 *
 * The read / prog callbacks use c->block_size at runtime (not a
 * compile-time constant), so the same binary can mount filesystems
 * formatted with different block sizes (e.g. 512 bytes by the PC
 * littlefs-fuse tool, or 4096 bytes by this library's default).
 */

#include "lfs_bd_sdcard.h"
#include "sdcard.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Set to 1 to print every block-device I/O call (verbose debug). */
#define LFS_BD_TRACE 0

/* =========================================================================
 * Default geometry
 * ========================================================================= */

#ifndef LFS_BD_SDCARD_DEFAULT_BLOCK_SIZE
#define LFS_BD_SDCARD_DEFAULT_BLOCK_SIZE 16384
#endif

#ifndef LFS_BD_SDCARD_DEFAULT_CACHE_SIZE
#define LFS_BD_SDCARD_DEFAULT_CACHE_SIZE 16384
#endif

/* =========================================================================
 * lfs_config callbacks  (use c->block_size, not a compile-time constant)
 * ========================================================================= */

static int bd_sdcard_read(const struct lfs_config *c, lfs_block_t block,
			  lfs_off_t off, void *buffer, lfs_size_t size)
{
	lfs_bd_sdcard_t *bd = (lfs_bd_sdcard_t *)c->context;

	/* Convert littlefs (block, off) → absolute byte address → SD sector.
     * Must use 64-bit for the byte address: block * block_size can exceed
     * 4 GiB on SDHC/SDXC cards (e.g. block 8,413,785 × 512 = 4.3 GB). */
	uint64_t byte_addr = (uint64_t)block * c->block_size + off;
	uint32_t sector = bd->sector_offset + (uint32_t)(byte_addr / 512);
	uint32_t sectors = size / 512;
	uint8_t *dst = (uint8_t *)buffer;

	if (LFS_BD_TRACE) {
		printf("  [rd] blk=%" PRIu32 " off=%" PRIu32 " size=%" PRIu32
		       " bs=%" PRIu32 " → sec=%" PRIu32 "+%" PRIu32 "\n",
		       block, off, size, c->block_size, sector, sectors);
	}

	/* Use multi-block read (CMD18) for consecutive sectors — pays the
     * card's ~10 ms access latency once instead of per sector. */
	int err = sd_read_blocks(sector, dst, sectors);
	if (err != SD_OK) {
		if (LFS_BD_TRACE)
			printf("  [rd] sd_read_blocks(%" PRIu32 ", %" PRIu32
			       ") FAIL err=%d\n",
			       sector, sectors, err);
		return LFS_ERR_IO;
	}

	return LFS_ERR_OK;
}

static int bd_sdcard_prog(const struct lfs_config *c, lfs_block_t block,
			  lfs_off_t off, const void *buffer, lfs_size_t size)
{
	lfs_bd_sdcard_t *bd = (lfs_bd_sdcard_t *)c->context;

	/* Must use 64-bit — see note in bd_sdcard_read above. */
	uint64_t byte_addr = (uint64_t)block * c->block_size + off;
	uint32_t sector = bd->sector_offset + (uint32_t)(byte_addr / 512);
	uint32_t sectors = size / 512;
	const uint8_t *src = (const uint8_t *)buffer;

	if (LFS_BD_TRACE) {
		printf("  [wr] blk=%" PRIu32 " off=%" PRIu32 " size=%" PRIu32
		       " bs=%" PRIu32 " → sec=%" PRIu32 "+%" PRIu32 "\n",
		       block, off, size, c->block_size, sector, sectors);
	}

	for (uint32_t i = 0; i < sectors; i++) {
		int err = sd_write_block(sector + i, src + i * 512);
		if (err != SD_OK) {
			if (LFS_BD_TRACE)
				printf("  [wr] sd_write_block(%" PRIu32
				       ") FAIL err=%d\n",
				       sector + i, err);
			return LFS_ERR_IO;
		}
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
	if (!cfg || !bd)
		return LFS_ERR_INVAL;

	memset(cfg, 0, sizeof(*cfg));

	/* ---- geometry --------------------------------------------------- */
	cfg->read_size = 512;
	cfg->prog_size = 512;
	cfg->block_size = LFS_BD_SDCARD_DEFAULT_BLOCK_SIZE;

	/* Default block_count: query the card for its real capacity.
     * If sector_count_limit is non-zero it overrides the detected value.
     * Falls back to a conservative maximum if the card can't be queried. */
	uint32_t total_sectors = bd->sector_count_limit;
	if (total_sectors == 0) {
		total_sectors = sd_get_sector_count();
		if (total_sectors == 0)
			total_sectors = 0xFFFFFFFF; /* last-resort fallback */
	}
	uint32_t usable_sectors = total_sectors - bd->sector_offset;
	cfg->block_count = usable_sectors / (cfg->block_size / 512);

	cfg->block_cycles = 500;
	cfg->cache_size = LFS_BD_SDCARD_DEFAULT_CACHE_SIZE;
	cfg->lookahead_size = 256; /* track 2048 blocks at a time */

	/* ---- callbacks -------------------------------------------------- */
	cfg->context = bd;
	cfg->read = bd_sdcard_read;
	cfg->prog = bd_sdcard_prog;
	cfg->erase = bd_sdcard_erase;
	cfg->sync = bd_sdcard_sync;

	/* caller is responsible for optional static buffers:
     *   cfg->read_buffer
     *   cfg->prog_buffer
     *   cfg->lookahead_buffer
     */

	return LFS_ERR_OK;
}

/* =========================================================================
 * Geometry auto-detect
 *
 * Tries lfs_mount() with a sequence of common block sizes.  The first
 * one that succeeds wins.  cfg->block_size, cfg->block_count, and
 * cfg->cache_size are updated to match the on-disk geometry.
 *
 * This allows a Pico to mount a filesystem that was formatted on a PC
 * with the littlefs-fuse tool (which uses block_size = 512), while
 * still using the library's own default (4096) for new filesystems.
 * ========================================================================= */

int lfs_bd_sdcard_mount_auto(lfs_t *lfs, struct lfs_config *cfg,
			     lfs_bd_sdcard_t *bd)
{
	/* Block sizes to probe — ordered by preference (our default first).
     *   4096 = this library's default
     *   512  = littlefs-fuse / lfs PC tool default (BLKSSZGET → sector size)
     * Other common sizes for NOR flash etc. can be added here. */
	static const lfs_size_t probes[] = { 16384, 4096, 512, 32768, 131072 };
	static const int nprobes = sizeof(probes) / sizeof(probes[0]);

	/* Save the caller's preferred geometry */
	lfs_size_t orig_block_size = cfg->block_size;
	lfs_size_t orig_block_count = cfg->block_count;
	lfs_size_t orig_cache_size = cfg->cache_size;

	for (int i = 0; i < nprobes; i++) {
		lfs_size_t bs = probes[i];

		/* Derive geometry from the probe block size */
		uint32_t total_sectors = bd->sector_count_limit;
		if (total_sectors == 0) {
			total_sectors = sd_get_sector_count();
			if (total_sectors == 0)
				total_sectors = 0xFFFFFFFF;
		}
		uint32_t usable_sectors = total_sectors - bd->sector_offset;

		cfg->block_size = bs;
		cfg->block_count = usable_sectors / (bs / 512);
		cfg->cache_size = bs; /* one block cache */

		/* static buffers must be ≥ cache_size; the caller owns this check.
         * If the probe block size is larger than the caller's buffers,
         * skip it — lfs_mount would overflow the buffer. */
		if (cfg->read_buffer && bs > orig_cache_size)
			continue;
		if (cfg->prog_buffer && bs > orig_cache_size)
			continue;

		/* A failed lfs_mount() can leave stale caches in lfs_t
         * that confuse the next probe — start fresh each time. */
		memset(lfs, 0, sizeof(*lfs));
		int err = lfs_mount(lfs, cfg);
		if (err == LFS_ERR_OK)
			return LFS_ERR_OK; /* mounted with probed geometry */

		/* LFS_ERR_INVAL → geometry mismatch, try next size.
         * Any other error → card genuinely not formatted;
         * stop probing and restore the caller's defaults. */
		if (err != LFS_ERR_INVAL) {
			cfg->block_size = orig_block_size;
			cfg->block_count = orig_block_count;
			cfg->cache_size = orig_cache_size;
			return err;
		}
	}

	/* Nothing matched — restore defaults and report */
	cfg->block_size = orig_block_size;
	cfg->block_count = orig_block_count;
	cfg->cache_size = orig_cache_size;
	return LFS_ERR_INVAL;
}
