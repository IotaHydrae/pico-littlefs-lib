/*
 * pico-littlefs Large File Read Benchmark
 *
 * Reads a large file from littlefs in fixed-size chunks, measures
 * throughput, and prints progress.  Useful for verifying SD card
 * read performance under sustained I/O.
 *
 * Preparation (on PC):
 *   1. Mount the SD card:  ./lfs --block_size=4096 /dev/sda mount
 *   2. Create a large file:
 *        dd if=/dev/urandom of=/mountpoint/data/large.bin bs=1M count=100
 *      or copy an existing file:
 *        cp some-large-video.mp4 /mountpoint/data/large.bin
 *   3. Unmount and insert the SD card into the Pico
 *
 * Build & flash:
 *   ninja && sudo picotool load -fx ./example/pico-large-read.uf2
 *
 * Monitor:
 *   minicom -D /dev/ttyACM0
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lfs.h"
#include "lfs_bd_sdcard.h"
#include "sdcard.h"
#include "sdcard_port.h"
#include "sdcard_port_pico.h"

/* =========================================================================
 * Configuration
 * ========================================================================= */

#define TEST_FILE_PATH "/data/large.bin"
#define CHUNK_SIZE 8192 /* bytes per read call */
#define PROGRESS_INTERVAL (1024 * 1024) /* print a dot every 1 MiB */

/* =========================================================================
 * Static allocations — no heap
 * ========================================================================= */

static lfs_bd_sdcard_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

static uint8_t read_buffer[16384];
static uint8_t prog_buffer[16384];
static uint32_t lookahead_buffer[256 / sizeof(uint32_t)];
static uint8_t chunk[CHUNK_SIZE];

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

static const char *card_type_str(sd_card_type_t t)
{
	switch (t) {
	case SD_TYPE_SDSC:
		return "SDSC";
	case SD_TYPE_SDHC:
		return "SDHC/SDXC";
	default:
		return "Unknown";
	}
}

static void print_divider(void)
{
	printf("----------------------------------------\n");
}

/* Format a byte count with human-readable units (e.g. "12.34 MiB"). */
static void print_size(uint32_t bytes)
{
	if (bytes >= 1024 * 1024 * 1024) {
		printf("%.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
	} else if (bytes >= 1024 * 1024) {
		printf("%.2f MiB", (double)bytes / (1024.0 * 1024.0));
	} else if (bytes >= 1024) {
		printf("%.2f KiB", (double)bytes / 1024.0);
	} else {
		printf("%" PRIu32 " B", bytes);
	}
}

/* Format a duration in µs as a human-readable string (s or ms). */
static void print_duration(uint64_t us)
{
	if (us >= 1000000ULL) {
		printf("%.3f s", (double)us / 1000000.0);
	} else {
		printf("%.2f ms", (double)us / 1000.0);
	}
}

/* Format a throughput in bytes per second. */
static void print_throughput(uint32_t bytes, uint64_t us)
{
	double bps = (double)bytes / ((double)us / 1000000.0);
	if (bps >= 1024.0 * 1024.0) {
		printf("%.2f MiB/s", bps / (1024.0 * 1024.0));
	} else if (bps >= 1024.0) {
		printf("%.2f KiB/s", bps / 1024.0);
	} else {
		printf("%.0f B/s", bps);
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
	printf("  pico-littlefs Large File Read Test\n");
	printf("========================================\n\n");

	/* ---- 1. Initialise SD card ---------------------------------------- */
	printf("[1/4] Initialising SD card ...\n");
	int err = sd_init();
	if (err != SD_OK) {
		printf("  FAIL: %s (code %d)\n", sd_error_str(err), err);

		/* SPI bus diagnostic (same as main demo) */
		printf("\n  --- SPI bus diagnostic ---\n");
		printf("  Pins: MISO=GP%d MOSI=GP%d SCK=GP%d CS=GP%d\n",
		       SD_PORT_PICO_PIN_MISO, SD_PORT_PICO_PIN_MOSI,
		       SD_PORT_PICO_PIN_SCK, SD_PORT_PICO_PIN_CS);
		printf("  Sending 16 dummy clocks (CS high) ...\n  ");
		for (int i = 0; i < 16; i++)
			printf("%02X ", sd_port_spi_rw(0xFF));
		printf("\n");
		while (1)
			sleep_ms(1000);
	}
	printf("  OK — %s, SPI %lu Hz\n\n", card_type_str(sd_get_type()),
	       (unsigned long)sd_get_baudrate());

	/* ---- 2. Mount littlefs -------------------------------------------- */
	printf("[2/4] Mounting filesystem ...\n");

	err = lfs_bd_sdcard_init(&cfg, &bd);
	if (err != LFS_ERR_OK) {
		printf("  Config FAIL: %s\n", lfs_err_str(err));
		while (1)
			sleep_ms(1000);
	}

	cfg.read_buffer = read_buffer;
	cfg.prog_buffer = prog_buffer;
	cfg.lookahead_buffer = lookahead_buffer;

	err = lfs_bd_sdcard_mount_auto(&lfs, &cfg, &bd);
	if (err != LFS_ERR_OK) {
		printf("  Mount FAIL: %s (code %d)\n", lfs_err_str(err), err);
		printf("  Is the card formatted? Run the main demo first.\n");
		while (1)
			sleep_ms(1000);
	}
	printf("  Mounted OK (block_size=%lu).\n\n",
	       (unsigned long)cfg.block_size);

	/* ---- 3. Open file ------------------------------------------------- */
	printf("[3/4] Opening '%s' ...\n", TEST_FILE_PATH);

	lfs_file_t file;
	err = lfs_file_open(&lfs, &file, TEST_FILE_PATH, LFS_O_RDONLY);
	if (err != LFS_ERR_OK) {
		printf("  Open FAIL: %s (code %d)\n", lfs_err_str(err), err);
		printf("\n  Create this file on the PC side first:\n");
		printf("    ./lfs --block_size=4096 /dev/sda mount\n");
		printf("    mkdir -p <mount>/data\n");
		printf("    dd if=/dev/urandom of=<mount>/data/large.bin "
		       "bs=1M count=100\n");
		printf("    fusermount -u <mount>\n");
		while (1)
			sleep_ms(1000);
	}

	lfs_soff_t file_size = lfs_file_size(&lfs, &file);
	printf("  File size: %ld bytes (", (long)file_size);
	print_size((uint32_t)file_size);
	printf(")\n");
	printf("  Read chunk: %u bytes\n", CHUNK_SIZE);
	printf("  Progress:  each '.' = ");
	print_size(PROGRESS_INTERVAL);
	printf("\n\n");

	/* ---- 4. Read the file in chunks ----------------------------------- */
	printf("[4/4] Reading ...\n  ");

	uint32_t total_read = 0;
	uint32_t progress_next = PROGRESS_INTERVAL;
	uint64_t t_start = time_us_64();

	while (1) {
		lfs_ssize_t n = lfs_file_read(&lfs, &file, chunk, CHUNK_SIZE);
		if (n < 0) {
			printf("\n  Read error at offset %" PRIu32 ": %s\n",
			       total_read, lfs_err_str((int)n));
			lfs_file_close(&lfs, &file);
			lfs_unmount(&lfs);
			while (1)
				sleep_ms(1000);
		}
		if (n == 0)
			break; /* EOF */

		total_read += (uint32_t)n;

		/* Progress indicator */
		while (total_read >= progress_next) {
			printf(".");
			progress_next += PROGRESS_INTERVAL;
		}
	}

	uint64_t t_end = time_us_64();
	uint64_t elapsed = t_end - t_start;

	lfs_file_close(&lfs, &file);

	/* ---- Summary ------------------------------------------------------ */
	printf("\n\n");
	print_divider();
	printf("  Read complete.\n\n");
	printf("  File:     %s\n", TEST_FILE_PATH);
	printf("  Size:     ");
	print_size(total_read);
	printf("\n");
	printf("  Time:     ");
	print_duration(elapsed);
	printf("\n");
	printf("  Speed:    ");
	print_throughput(total_read, elapsed);
	printf("\n");
	print_divider();

	/* ---- Unmount & loop ----------------------------------------------- */
	lfs_unmount(&lfs);

	printf("\n========================================\n");
	printf("  Test complete.\n");
	printf("========================================\n\n");

	for (;;)
		sleep_ms(1000);
}
