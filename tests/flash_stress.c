/*
 * SPI Flash Stability Test
 *
 * Repeatedly formats, mounts, writes files with known patterns,
 * reads them back, and verifies data integrity.  Reports pass/fail
 * counts over multiple cycles.
 *
 * This stresses the flash erase-write-verify pipeline and helps
 * catch intermittent hardware or timing issues.
 *
 * Flash:  sudo picotool load -fx ./tests/pico-flash-stress.uf2
 * Monitor: minicom -D /dev/ttyACM0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "flash.h"
#include "lfs.h"
#include "lfs_bd_flash.h"

/* =========================================================================
 * Configuration
 * ========================================================================= */

#define TEST_CYCLES      100    /* number of format-mount-verify cycles */
#define FILE_COUNT       4      /* files per cycle */
#define FILE_SIZE        1024   /* bytes per file */
#define PATTERN_SEED     0xA5   /* base byte for fill pattern */

/* =========================================================================
 * Static buffers
 * ========================================================================= */

static lfs_bd_flash_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

static uint8_t read_buffer[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint8_t prog_buffer[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint32_t lookahead_buffer[256 / sizeof(uint32_t)];

static uint8_t wr_buf[FILE_SIZE];
static uint8_t rd_buf[FILE_SIZE];

/* =========================================================================
 * Helpers
 * ========================================================================= */

static const char *lfs_err(int err)
{
	switch (err) {
	case LFS_ERR_OK:
		return "OK";
	case LFS_ERR_IO:
		return "IO";
	case LFS_ERR_CORRUPT:
		return "CORRUPT";
	case LFS_ERR_NOSPC:
		return "NOSPC";
	default:
		return "?";
	}
}

static void fill_pattern(uint8_t *buf, uint32_t size, uint8_t seed)
{
	for (uint32_t i = 0; i < size; i++)
		buf[i] = (uint8_t)(seed + i);
}

static int verify_pattern(const uint8_t *buf, uint32_t size, uint8_t seed)
{
	for (uint32_t i = 0; i < size; i++) {
		if (buf[i] != (uint8_t)(seed + i))
			return (int)i; /* offset of first mismatch */
	}
	return -1; /* all good */
}

/* =========================================================================
 * One test cycle: format → write files → verify → report
 * ========================================================================= */

static int run_one_cycle(int cycle)
{
	int errors = 0;

	/* ---- format -------------------------------------------------- */
	int err = lfs_format(&lfs, &cfg);
	if (err != LFS_ERR_OK) {
		printf("  cycle %d: format FAIL (%s)\n", cycle, lfs_err(err));
		return 1;
	}

	/* ---- mount --------------------------------------------------- */
	err = lfs_mount(&lfs, &cfg);
	if (err != LFS_ERR_OK) {
		printf("  cycle %d: mount FAIL (%s)\n", cycle, lfs_err(err));
		return 1;
	}

	/* ---- write files --------------------------------------------- */
	for (int f = 0; f < FILE_COUNT; f++) {
		char name[16];
		snprintf(name, sizeof(name), "test%d.bin", f);

		lfs_file_t file;
		err = lfs_file_open(&lfs, &file, name,
				    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
		if (err != LFS_ERR_OK) {
			printf("  cycle %d: open(w) %s FAIL (%s)\n", cycle,
			       name, lfs_err(err));
			errors++;
			continue;
		}

		uint8_t seed = (uint8_t)(PATTERN_SEED + f + cycle);
		fill_pattern(wr_buf, FILE_SIZE, seed);

		lfs_ssize_t n = lfs_file_write(&lfs, &file, wr_buf, FILE_SIZE);
		if (n != FILE_SIZE) {
			printf("  cycle %d: write %s FAIL (%ld/%d)\n", cycle,
			       name, (long)n, FILE_SIZE);
			errors++;
		}

		lfs_file_close(&lfs, &file);
	}

	/* ---- read back and verify ----------------------------------- */
	for (int f = 0; f < FILE_COUNT; f++) {
		char name[16];
		snprintf(name, sizeof(name), "test%d.bin", f);

		lfs_file_t file;
		err = lfs_file_open(&lfs, &file, name, LFS_O_RDONLY);
		if (err != LFS_ERR_OK) {
			printf("  cycle %d: open(r) %s FAIL (%s)\n", cycle,
			       name, lfs_err(err));
			errors++;
			continue;
		}

		memset(rd_buf, 0, FILE_SIZE);
		lfs_ssize_t n = lfs_file_read(&lfs, &file, rd_buf, FILE_SIZE);
		lfs_file_close(&lfs, &file);

		if (n != FILE_SIZE) {
			printf("  cycle %d: read %s FAIL (%ld/%d)\n", cycle,
			       name, (long)n, FILE_SIZE);
			errors++;
			continue;
		}

		uint8_t seed = (uint8_t)(PATTERN_SEED + f + cycle);
		int mismatch = verify_pattern(rd_buf, FILE_SIZE, seed);
		if (mismatch >= 0) {
			printf("  cycle %d: verify %s FAIL at offset %d\n",
			       cycle, name, mismatch);
			errors++;
		}
	}

	lfs_unmount(&lfs);
	return errors;
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
	stdio_init_all();
	sleep_ms(2000);

	printf("\n");
	printf("========================================\n");
	printf("  SPI Flash Stability Test\n");
	printf("  Cycles: %d  |  Files: %d  |  Size: %d B\n",
	       TEST_CYCLES, FILE_COUNT, FILE_SIZE);
	printf("========================================\n\n");

	/* ---- 1. Init flash --------------------------------------------- */
	printf("[1/3] Initialising SPI flash ...\n");
	int err = spi_flash_init();
	if (err != SPI_FLASH_OK) {
		printf("  FAIL: %s (code %d)\n", spi_flash_error_str(err), err);
		while (1)
			sleep_ms(1000);
	}

	spi_flash_info_t info;
	spi_flash_get_info(&info);
	printf("  OK — %" PRIu32 " KiB, mfr=0x%02X\n\n",
	       info.size_bytes / 1024, info.manufacturer_id);

	/* ---- 2. Configure littlefs ------------------------------------- */
	printf("[2/3] Configuring littlefs ...\n");
	err = lfs_bd_flash_init(&cfg, &bd);
	if (err != LFS_ERR_OK) {
		printf("  FAIL: %s\n", lfs_err(err));
		while (1)
			sleep_ms(1000);
	}

	cfg.read_buffer = read_buffer;
	cfg.prog_buffer = prog_buffer;
	cfg.lookahead_buffer = lookahead_buffer;

	printf("  block_size=%" PRIu32 "  block_count=%" PRIu32 "\n\n",
	       cfg.block_size, cfg.block_count);

	/* ---- 3. Run test cycles ---------------------------------------- */
	printf("[3/3] Running %d cycles ...\n\n", TEST_CYCLES);

	int passes = 0;
	int fails = 0;

	for (int cycle = 1; cycle <= TEST_CYCLES; cycle++) {
		int e = run_one_cycle(cycle);
		if (e == 0) {
			passes++;
			if (cycle % 10 == 0)
				printf("  [%3d/%3d] OK  (pass:%d fail:%d)\n",
				       cycle, TEST_CYCLES, passes, fails);
		} else {
			fails++;
		}

		/* Abort on consecutive failures (hardware likely dead) */
		if (fails > 3 && fails > passes) {
			printf("\n  Too many failures — aborting.\n");
			break;
		}
	}

	/* ---- Summary ---------------------------------------------------- */
	printf("\n========================================\n");
	printf("  Test complete.\n");
	printf("  Passes:  %d\n", passes);
	printf("  Failures: %d\n", fails);
	printf("========================================\n\n");

	for (;;)
		sleep_ms(1000);
}
