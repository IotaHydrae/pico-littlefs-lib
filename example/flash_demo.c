/*
 * pico-littlefs SPI Flash Demo
 *
 * Exercises littlefs on SPI NOR flash:
 *   1. Initialise flash chip
 *   2. Format with littlefs
 *   3. Mount
 *   4. Create a file, write to it, read it back
 *   5. List root directory
 *   6. Unmount
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "flash.h"
#include "lfs.h"
#include "lfs_bd_flash.h"

/* =========================================================================
 * Static buffers
 * ========================================================================= */

static lfs_bd_flash_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

static uint8_t read_buffer[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint8_t prog_buffer[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint32_t lookahead_buffer[256 / sizeof(uint32_t)];

/* =========================================================================
 * Helpers
 * ========================================================================= */

static const char *lfs_err_str(int err)
{
	switch (err) {
	case LFS_ERR_OK:
		return "OK";
	case LFS_ERR_IO:
		return "IO error";
	case LFS_ERR_CORRUPT:
		return "Corrupted";
	case LFS_ERR_NOENT:
		return "No such entry";
	case LFS_ERR_EXIST:
		return "Already exists";
	case LFS_ERR_NOTDIR:
		return "Not a directory";
	case LFS_ERR_ISDIR:
		return "Is a directory";
	case LFS_ERR_NOTEMPTY:
		return "Directory not empty";
	case LFS_ERR_BADF:
		return "Bad file descriptor";
	case LFS_ERR_FBIG:
		return "File too large";
	case LFS_ERR_INVAL:
		return "Invalid parameter";
	case LFS_ERR_NOSPC:
		return "No space left";
	case LFS_ERR_NOMEM:
		return "No memory";
	case LFS_ERR_NAMETOOLONG:
		return "Name too long";
	default:
		return "Unknown";
	}
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
	printf("  pico-littlefs SPI Flash Demo\n");
	printf("========================================\n\n");

	/* ---- 1. Initialise flash chip ----------------------------------- */
	printf("[1/5] Initialising SPI flash ...\n");
	int err = spi_flash_init();
	if (err != SPI_FLASH_OK) {
		printf("  FAIL: %s (code %d)\n", spi_flash_error_str(err), err);
		while (1)
			sleep_ms(1000);
	}

	spi_flash_info_t info;
	spi_flash_get_info(&info);
	printf("  OK — %lu KiB NOR flash (mfr=0x%02X, type=0x%02X)\n\n",
	       (unsigned long)(info.size_bytes / 1024), info.manufacturer_id,
	       info.memory_type);

	/* ---- 2. Configure littlefs block device ------------------------- */
	printf("[2/5] Configuring littlefs ...\n");
	err = lfs_bd_flash_init(&cfg, &bd);
	if (err != LFS_ERR_OK) {
		printf("  FAIL: %s (code %d)\n", lfs_err_str(err), err);
		while (1)
			sleep_ms(1000);
	}

	cfg.read_buffer = read_buffer;
	cfg.prog_buffer = prog_buffer;
	cfg.lookahead_buffer = lookahead_buffer;

	/* ---- 3. Mount (format if needed) -------------------------------- */
	printf("[3/5] Mounting filesystem ...\n");
	err = lfs_mount(&lfs, &cfg);
	if (err == LFS_ERR_CORRUPT) {
		printf("  Not formatted — formatting ...\n");
		err = lfs_format(&lfs, &cfg);
		if (err != LFS_ERR_OK) {
			printf("  Format FAIL: %s\n", lfs_err_str(err));
			while (1)
				sleep_ms(1000);
		}
		printf("  Format OK.\n");
		err = lfs_mount(&lfs, &cfg);
	}
	if (err != LFS_ERR_OK) {
		printf("  Mount FAIL: %s\n", lfs_err_str(err));
		while (1)
			sleep_ms(1000);
	}
	printf("  Mounted OK (block_size=%lu, block_count=%lu).\n",
	       (unsigned long)cfg.block_size, (unsigned long)cfg.block_count);
	printf("  → filesystem capacity: %lu KiB\n\n",
	       (unsigned long)((uint64_t)cfg.block_count * cfg.block_size /
			       1024));

	/* ---- 4. File write + read test ---------------------------------- */
	printf("[4/5] File write / read test ...\n");

	const char *filename = "hello.txt";
	const char *message = "Hello from pico-littlefs on SPI NOR flash!\r\n";

	lfs_file_t file;
	err = lfs_file_open(&lfs, &file, filename,
			    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
	if (err != LFS_ERR_OK) {
		printf("  lfs_file_open(w) FAIL: %s\n", lfs_err_str(err));
		while (1)
			sleep_ms(1000);
	}

	lfs_size_t wrote = lfs_file_write(&lfs, &file, message, strlen(message));
	lfs_file_close(&lfs, &file);
	printf("  Wrote %lu bytes to '%s'.\n", (unsigned long)wrote, filename);

	char readback[256];
	err = lfs_file_open(&lfs, &file, filename, LFS_O_RDONLY);
	if (err == LFS_ERR_OK) {
		lfs_ssize_t n = lfs_file_read(&lfs, &file, readback,
					      sizeof(readback) - 1);
		readback[n > 0 ? n : 0] = '\0';
		printf("  Read back: '%s'\n", readback);
		lfs_file_close(&lfs, &file);
	}

	/* ---- 5. List root directory ------------------------------------- */
	printf("\n[5/5] Listing root directory ...\n");
	lfs_dir_t dir;
	err = lfs_dir_open(&lfs, &dir, "/");
	if (err == LFS_ERR_OK) {
		struct lfs_info entry;
		int count = 0;
		while (lfs_dir_read(&lfs, &dir, &entry) > 0) {
			printf("  %c %8ld  %s\n",
			       entry.type == LFS_TYPE_DIR ? 'd' : 'f',
			       (long)entry.size, entry.name);
			count++;
		}
		printf("  → %d entries\n", count);
		lfs_dir_close(&lfs, &dir);
	}

	lfs_unmount(&lfs);

	printf("\n========================================\n");
	printf("  Demo complete.\n");
	printf("========================================\n\n");

	for (;;)
		sleep_ms(1000);
}
