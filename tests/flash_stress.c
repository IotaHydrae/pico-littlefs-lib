/*
 * SPI Flash 24-Hour Stability Test
 *
 * Simulates real-world filesystem usage over an extended period:
 * creates, writes, reads, verifies, and deletes files of varying
 * sizes, filling the flash to ~70 % then rotating old files out.
 *
 * Reports status every 30 minutes.  Runs until the configured
 * duration or indefinitely if DURATION_HOURS is 0.
 *
 * Flash:  sudo picotool load -fx ./tests/pico-flash-stress.uf2
 * Monitor: minicom -D /dev/ttyACM0
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "flash.h"
#include "lfs.h"
#include "lfs_bd_flash.h"

/* =========================================================================
 * Configuration
 * ========================================================================= */

#ifndef DURATION_HOURS
#define DURATION_HOURS       24      /* 0 = run forever */
#endif
#ifndef STATUS_INTERVAL_MIN
#define STATUS_INTERVAL_MIN  30      /* print status every N minutes */
#endif
#define TARGET_USAGE_PCT     70      /* keep flash ~70 % full */
#define MAX_FILES            128     /* max live files at once */
#define MIN_FILE_SIZE        256     /* smallest test file */
#define MAX_FILE_SIZE        (128 * 1024)  /* largest test file */
#define IO_CHUNK_SIZE        4096    /* read/write chunk size */

/* =========================================================================
 * Static buffers
 * ========================================================================= */

static lfs_bd_flash_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

static uint8_t read_buf[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint8_t prog_buf[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint32_t lookahead[256 / sizeof(uint32_t)];

static uint8_t io_buf[MAX_FILE_SIZE];  /* single buffer shared by all ops */

/* =========================================================================
 * Statistics
 * ========================================================================= */

typedef struct {
	uint32_t ops_total;        /* all filesystem operations      */
	uint32_t ops_create;
	uint32_t ops_write;
	uint32_t ops_read;
	uint32_t ops_delete;
	uint32_t ops_list;
	uint32_t bytes_written;
	uint32_t bytes_read;
	uint32_t verify_errors;    /* data mismatch count            */
	uint32_t fs_errors;        /* lfs/spi errors                 */
	uint32_t files_live;       /* current file count             */
	uint32_t max_files_seen;   /* peak file count                */
	uint64_t total_us;         /* elapsed wall-clock time        */
} stats_t;

static stats_t s;

/* =========================================================================
 * Helpers
 * ========================================================================= */

static const char *lfs_str(int err)
{
	switch (err) {
	case LFS_ERR_OK:      return "OK";
	case LFS_ERR_IO:      return "IO";
	case LFS_ERR_CORRUPT: return "CORRUPT";
	case LFS_ERR_NOSPC:   return "NOSPC";
	default:              return "?";
	}
}

static uint32_t elapsed_min(void)
{
	return (uint32_t)(s.total_us / 60000000ULL);
}

/* --------------------------------------------------------------------------
 * Fill buffer with a repeatable pseudo-random pattern (not rand() so
 * we can verify deterministically).  Based on file index + offset.
 * -------------------------------------------------------------------------- */

static void fill_pattern(uint8_t *buf, uint32_t size, uint32_t file_id,
			 uint32_t offset)
{
	for (uint32_t i = 0; i < size; i++) {
		uint32_t x = file_id * 0x9E3779B1u + offset + i;
		x ^= x >> 13;
		x *= 0x85EBCA77u;
		x ^= x >> 16;
		buf[i] = (uint8_t)(x & 0xFF);
	}
}

static int verify_pattern(const uint8_t *buf, uint32_t size, uint32_t file_id,
			  uint32_t offset)
{
	for (uint32_t i = 0; i < size; i++) {
		uint32_t x = file_id * 0x9E3779B1u + offset + i;
		x ^= x >> 13;
		x *= 0x85EBCA77u;
		x ^= x >> 16;
		if (buf[i] != (uint8_t)(x & 0xFF))
			return (int)i;
	}
	return -1;
}

/* --------------------------------------------------------------------------
 * Simple xorshift PRNG (deterministic, small footprint)
 * -------------------------------------------------------------------------- */

static uint32_t xorshift_state = 0xDEADBEEFu;

static uint32_t xorshift(void)
{
	uint32_t x = xorshift_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xorshift_state = x;
	return x;
}

/* =========================================================================
 * Filesystem operations
 * ========================================================================= */

static int fs_usage_pct(void)
{
	lfs_ssize_t used = lfs_fs_size(&lfs);
	if (used < 0)
		return 100;
	return (int)((uint64_t)used * 100 / cfg.block_count);
}

static int do_create(uint32_t file_id, uint32_t *out_size)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);

	lfs_file_t file;
	int err = lfs_file_open(&lfs, &file, name,
				LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
	if (err < 0)
		return err;

	uint32_t total = MIN_FILE_SIZE +
			 (xorshift() % (MAX_FILE_SIZE - MIN_FILE_SIZE + 1));
	uint32_t written = 0;

	while (written < total) {
		uint32_t chunk = total - written;
		if (chunk > IO_CHUNK_SIZE)
			chunk = IO_CHUNK_SIZE;

		fill_pattern(io_buf, chunk, file_id, written);
		lfs_ssize_t n = lfs_file_write(&lfs, &file, io_buf, chunk);
		if (n < 0) {
			lfs_file_close(&lfs, &file);
			return (int)n;
		}
		written += (uint32_t)n;
	}

	lfs_file_close(&lfs, &file);
	s.bytes_written += written;
	if (out_size)
		*out_size = written;
	return 0;
}

static int do_read_verify(uint32_t file_id, uint32_t expected_size)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);

	lfs_file_t file;
	int err = lfs_file_open(&lfs, &file, name, LFS_O_RDONLY);
	if (err < 0)
		return err;

	lfs_soff_t actual = lfs_file_size(&lfs, &file);
	if ((uint32_t)actual != expected_size) {
		lfs_file_close(&lfs, &file);
		return LFS_ERR_CORRUPT;
	}

	uint32_t offset = 0;
	while (offset < expected_size) {
		uint32_t chunk = expected_size - offset;
		if (chunk > IO_CHUNK_SIZE)
			chunk = IO_CHUNK_SIZE;

		lfs_ssize_t n = lfs_file_read(&lfs, &file, io_buf, chunk);
		if (n < 0) {
			lfs_file_close(&lfs, &file);
			return (int)n;
		}

		int bad = verify_pattern(io_buf, (uint32_t)n, file_id, offset);
		if (bad >= 0) {
			lfs_file_close(&lfs, &file);
			return LFS_ERR_CORRUPT;
		}

		offset += (uint32_t)n;
		s.bytes_read += (uint32_t)n;
	}

	lfs_file_close(&lfs, &file);
	return 0;
}

static int do_delete(uint32_t file_id)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);
	return lfs_remove(&lfs, name);
}

/* =========================================================================
 * File tracking — simple fixed-size array of {id, size} pairs
 * ========================================================================= */

static struct {
	uint32_t id;
	uint32_t size;
} s_files[MAX_FILES];

static int s_files_find(uint32_t id)
{
	for (int i = 0; i < MAX_FILES; i++)
		if (s_files[i].id == id)
			return i;
	return -1;
}

static void s_files_add(uint32_t id, uint32_t size)
{
	int slot = -1;
	for (int i = 0; i < MAX_FILES; i++) {
		if (s_files[i].id == 0) { slot = i; break; }
	}
	if (slot < 0)
		slot = (int)(xorshift() % MAX_FILES); /* overwrite oldest */
	s_files[slot].id = id;
	s_files[slot].size = size;
	s.files_live++;
}

static void s_files_del(uint32_t id)
{
	int idx = s_files_find(id);
	if (idx >= 0) {
		s_files[idx].id = 0;
		s_files[idx].size = 0;
		s.files_live--;
	}
}

static int s_files_random(void)
{
	int tries = MAX_FILES * 2;
	while (tries-- > 0) {
		int i = (int)(xorshift() % MAX_FILES);
		if (s_files[i].id != 0)
			return i;
	}
	return -1;
}

/* =========================================================================
 * Status report
 * ========================================================================= */

static void print_status(void)
{
	uint32_t min = elapsed_min();
	uint32_t hr = min / 60;
	uint32_t mn = min % 60;

	printf("\n---- Status  %3" PRIu32 "h%02" PRIu32 "m  "
	       "ops:%" PRIu32 "  err:%" PRIu32 "  files:%" PRIu32
	       "  usage:%d%% ----\n"
	       "     create:%" PRIu32 " write:%" PRIu32 " read:%" PRIu32
	       " delete:%" PRIu32 " list:%" PRIu32 "\n"
	       "     written:%" PRIu32 " KiB  read:%" PRIu32
	       " KiB  verify_err:%" PRIu32 "\n",
	       hr, mn,
	       s.ops_total, s.fs_errors, s.files_live, fs_usage_pct(),
	       s.ops_create, s.ops_write, s.ops_read,
	       s.ops_delete, s.ops_list,
	       s.bytes_written / 1024, s.bytes_read / 1024,
	       s.verify_errors);
}

/* =========================================================================
 * Main loop
 * ========================================================================= */

static void run_one_op(void)
{
	s.ops_total++;

	/*
	 * Operation mix (weighted):
	 *   10 % create  (if we have room)
	 *   40 % read+verify (if files exist)
	 *   25 % write (append to existing)
	 *   15 % delete (if we're over target usage)
	 *   10 % list directory
	 */
	uint32_t r = xorshift() % 100;

	/* ---- list directory ---- */
	if (r < 10) {
		lfs_dir_t dir;
		if (lfs_dir_open(&lfs, &dir, "/") == LFS_ERR_OK) {
			struct lfs_info info;
			while (lfs_dir_read(&lfs, &dir, &info) > 0) {}
			lfs_dir_close(&lfs, &dir);
		}
		s.ops_list++;
		return;
	}
	r -= 10;

	/* ---- delete (if usage > target or randomly aged) ---- */
	if (r < 15) {
		int pct = fs_usage_pct();
		int idx = s_files_random();
		if (idx >= 0 && (pct > TARGET_USAGE_PCT || (xorshift() % 3) == 0)) {
			uint32_t id = s_files[idx].id;
			if (do_delete(id) == LFS_ERR_OK) {
				s_files_del(id);
				s.ops_delete++;
				return;
			}
			s.fs_errors++;
		}
		/* fall through to create if delete didn't happen */
	}
	r -= 15;

	/* ---- read + verify existing file ---- */
	if (r < 40) {
		int idx = s_files_random();
		if (idx >= 0) {
			uint32_t id = s_files[idx].id;
			uint32_t sz = s_files[idx].size;
			if (do_read_verify(id, sz) == LFS_ERR_OK) {
				s.ops_read++;
				return;
			}
			s.verify_errors++;
			s.fs_errors++;
			return;
		}
		/* fall through to create */
	}
	r -= 40;

	/* ---- write (append to existing file) ---- */
	if (r < 25) {
		int idx = s_files_random();
		if (idx >= 0) {
			uint32_t id = s_files[idx].id;
			char name[24];
			snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", id);
			lfs_file_t file;
			if (lfs_file_open(&lfs, &file, name,
					  LFS_O_WRONLY | LFS_O_APPEND) ==
			    LFS_ERR_OK) {
				uint32_t extra = (xorshift() % 2048) + 128;
				uint32_t old_sz = s_files[idx].size;
				fill_pattern(io_buf, extra, id, old_sz);
				lfs_ssize_t n = lfs_file_write(&lfs, &file,
								io_buf,
								extra);
				lfs_file_close(&lfs, &file);
				if (n > 0) {
					s_files[idx].size += (uint32_t)n;
					s.bytes_written += (uint32_t)n;
					s.ops_write++;
					return;
				}
			}
			s.fs_errors++;
			return;
		}
	}
	r -= 25;

	/* ---- create new file (if below target usage) ---- */
	if (fs_usage_pct() < TARGET_USAGE_PCT && s.files_live < MAX_FILES) {
		uint32_t id = (xorshift() & 0x7FFFFFFFu) | 1u; /* non-zero */
		uint32_t sz = 0;
		if (do_create(id, &sz) == LFS_ERR_OK) {
			s_files_add(id, sz);
			s.ops_create++;
			return;
		}
		s.fs_errors++;
		return;
	}

	/* Nothing to do — make room */
	if (s.files_live > 0) {
		int idx = s_files_random();
		if (idx >= 0) {
			do_delete(s_files[idx].id);
			s_files_del(s_files[idx].id);
			s.ops_delete++;
		}
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
	printf("  SPI Flash 24-Hour Stability Test\n");
	printf("  Duration: %d h  |  Max files: %d\n",
	       DURATION_HOURS, MAX_FILES);
	printf("  File sizes: %d B – %d KiB\n",
	       MIN_FILE_SIZE, MAX_FILE_SIZE / 1024);
	printf("  Target flash usage: %d %%\n",
	       TARGET_USAGE_PCT);
	printf("========================================\n\n");

	/* ---- 1. Init flash --------------------------------------------- */
	printf("[1/3] Initialising SPI flash ...\n");
	int err = spi_flash_init();
	if (err != SPI_FLASH_OK) {
		printf("  FAIL: %s (code %d)\n", spi_flash_error_str(err), err);
		while (1) sleep_ms(1000);
	}

	spi_flash_info_t info;
	spi_flash_get_info(&info);
	printf("  OK — %" PRIu32 " KiB, mfr=0x%02X\n\n",
	       info.size_bytes / 1024, info.manufacturer_id);

	/* ---- 2. Configure + format littlefs ---------------------------- */
	printf("[2/3] Formatting filesystem ...\n");
	err = lfs_bd_flash_init(&cfg, &bd);
	if (err != LFS_ERR_OK) {
		printf("  FAIL: %s\n", lfs_str(err));
		while (1) sleep_ms(1000);
	}

	cfg.read_buffer = read_buf;
	cfg.prog_buffer = prog_buf;
	cfg.lookahead_buffer = lookahead;

	err = lfs_format(&lfs, &cfg);
	if (err != LFS_ERR_OK) {
		printf("  Format FAIL: %s\n", lfs_str(err));
		while (1) sleep_ms(1000);
	}
	err = lfs_mount(&lfs, &cfg);
	if (err != LFS_ERR_OK) {
		printf("  Mount FAIL: %s\n", lfs_str(err));
		while (1) sleep_ms(1000);
	}
	printf("  Ready — %" PRIu32 " blocks, %" PRIu32 " KiB capacity\n\n",
	       cfg.block_count,
	       (uint32_t)((uint64_t)cfg.block_count * cfg.block_size / 1024));

	/* ---- 3. Run ---------------------------------------------------- */
	printf("[3/3] Running");
	if (DURATION_HOURS > 0)
		printf(" for %d hours", DURATION_HOURS);
	else
		printf(" indefinitely");
	printf(" (status every %d min) ...\n\n", STATUS_INTERVAL_MIN);

	uint64_t t_start = time_us_64();
	uint64_t t_next_status = t_start +
				 (uint64_t)STATUS_INTERVAL_MIN * 60 * 1000000;
	uint64_t t_end = DURATION_HOURS > 0
				 ? t_start +
					   (uint64_t)DURATION_HOURS * 3600 * 1000000
				 : UINT64_MAX;

	while (1) {
		run_one_op();
		s.total_us = time_us_64() - t_start;

		/* Status report */
		if (s.total_us >= t_next_status) {
			print_status();
			t_next_status += (uint64_t)STATUS_INTERVAL_MIN * 60 *
					 1000000;
		}

		/* Duration check */
		if (s.total_us >= t_end)
			break;

		/* Track peak files */
		if (s.files_live > s.max_files_seen)
			s.max_files_seen = s.files_live;
	}

	/* ---- Final summary --------------------------------------------- */
	lfs_unmount(&lfs);

	printf("\n========================================\n");
	printf("  Test finished.\n\n");
	printf("  Duration:  %" PRIu32 " min\n", elapsed_min());
	printf("  Total ops: %" PRIu32 "\n", s.ops_total);
	printf("  Create: %" PRIu32 "  Write: %" PRIu32 "  Read: %" PRIu32
	       "  Delete: %" PRIu32 "\n",
	       s.ops_create, s.ops_write, s.ops_read, s.ops_delete);
	printf("  Data written: %" PRIu32 " KiB   read: %" PRIu32 " KiB\n",
	       s.bytes_written / 1024, s.bytes_read / 1024);
	printf("  Peak files:   %" PRIu32 "\n", s.max_files_seen);
	if (s.verify_errors > 0 || s.fs_errors > 0)
		printf("  ERRORS — verify:%" PRIu32 "  fs:%" PRIu32 "\n",
		       s.verify_errors, s.fs_errors);
	else
		printf("  Errors: 0  —  flash is stable.\n");
	printf("========================================\n\n");

	for (;;)
		sleep_ms(1000);
}
