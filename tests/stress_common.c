/*
 * Stress Test Common Framework — Implementation
 *
 * Shared logic: operation loop, statistics, logging, file tracking,
 * data patterns, and status reporting.  Backend-specific code lives
 * in the per-device test files (flash_stress.c, etc.).
 */

#include "stress_common.h"

/* =========================================================================
 * Global state
 * ========================================================================= */

stress_stats_t s_st;

/* =========================================================================
 * Logging
 * ========================================================================= */

lfs_file_t s_log_file;
bool s_log_open;

int stress_log_init(void)
{
	int err = lfs_mkdir(&s_lfs, LOG_DIR);
	if (err != LFS_ERR_OK && err != LFS_ERR_EXIST)
		return err;

	err = lfs_file_open(&s_lfs, &s_log_file, LOG_FILE,
			    LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
	if (err == LFS_ERR_OK)
		s_log_open = true;
	return err;
}

void stress_log_write(const char *msg)
{
	printf("%s", msg);
	if (s_log_open) {
		lfs_file_write(&s_lfs, &s_log_file, msg, strlen(msg));
		lfs_file_sync(&s_lfs, &s_log_file);
	}
}

void stress_log_close(void)
{
	if (s_log_open) {
		lfs_file_close(&s_lfs, &s_log_file);
		s_log_open = false;
	}
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

const char *stress_lfs_str(int err)
{
	switch (err) {
	case LFS_ERR_OK:      return "OK";
	case LFS_ERR_IO:      return "IO";
	case LFS_ERR_CORRUPT: return "CORRUPT";
	case LFS_ERR_NOSPC:   return "NOSPC";
	default:              return "?";
	}
}

uint32_t stress_elapsed_min(void)
{
	return (uint32_t)(s_st.total_us / 60000000ULL);
}

int stress_fs_usage_pct(void)
{
	lfs_ssize_t used = lfs_fs_size(&s_lfs);
	if (used < 0) return 100;
	if (s_cfg.block_count == 0) return 0;
	/* Show fractional permille for large devices where sub-1% is common */
	uint32_t bp1000 = (uint32_t)((uint64_t)used * 1000 / s_cfg.block_count);
	/* round up: display 1% once we cross 0.5% */
	return (int)((bp1000 + 5) / 10);
}

void stress_print_status(void)
{
	uint32_t min = stress_elapsed_min();
	uint32_t hr = min / 60;
	uint32_t mn = min % 60;

	char buf[256];
	int n = snprintf(buf, sizeof(buf),
	       "\n---- Status  %3" PRIu32 "h%02" PRIu32 "m  "
	       "ops:%" PRIu32 "  err:%" PRIu32 "  files:%" PRIu32
	       "  usage:%d%% ----\n"
	       "     create:%" PRIu32 " write:%" PRIu32 " read:%" PRIu32
	       " delete:%" PRIu32 " list:%" PRIu32 "\n"
	       "     written:%" PRIu32 " KiB  read:%" PRIu32
	       " KiB  verify_err:%" PRIu32 "\n",
	       hr, mn,
	       s_st.ops_total, s_st.fs_errors, s_st.files_live,
	       stress_fs_usage_pct(),
	       s_st.ops_create, s_st.ops_write, s_st.ops_read,
	       s_st.ops_delete, s_st.ops_list,
	       s_st.bytes_written / 1024, s_st.bytes_read / 1024,
	       s_st.verify_errors);
	if (n > 0 && n < (int)sizeof(buf))
		stress_log_write(buf);
}

/* =========================================================================
 * File tracking
 * ========================================================================= */

static struct {
	uint32_t id;
	uint32_t size;
} s_files[MAX_FILES];

void stress_files_add(uint32_t id, uint32_t size)
{
	int slot = -1;
	for (int i = 0; i < MAX_FILES; i++) {
		if (s_files[i].id == 0) { slot = i; break; }
	}
	if (slot < 0)
		slot = (int)(stress_rand() % MAX_FILES);
	s_files[slot].id   = id;
	s_files[slot].size = size;
	s_st.files_live++;
}

void stress_files_del(uint32_t id)
{
	for (int i = 0; i < MAX_FILES; i++) {
		if (s_files[i].id == id) {
			s_files[i].id   = 0;
			s_files[i].size = 0;
			s_st.files_live--;
			return;
		}
	}
}

static int s_files_find(uint32_t id)
{
	for (int i = 0; i < MAX_FILES; i++)
		if (s_files[i].id == id) return i;
	return -1;
}

int stress_files_random(void)
{
	int tries = MAX_FILES * 2;
	while (tries-- > 0) {
		int i = (int)(stress_rand() % MAX_FILES);
		if (s_files[i].id != 0) return i;
	}
	return -1;
}

/* =========================================================================
 * Data patterns
 * ========================================================================= */

static uint32_t s_rand = 0xDEADBEEFu;

uint32_t stress_rand(void)
{
	uint32_t x = s_rand;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_rand = x;
	return x;
}

void stress_fill_pattern(uint8_t *buf, uint32_t size,
			 uint32_t file_id, uint32_t offset)
{
	for (uint32_t i = 0; i < size; i++) {
		uint32_t x = file_id * 0x9E3779B1u + offset + i;
		x ^= x >> 13;
		x *= 0x85EBCA77u;
		x ^= x >> 16;
		buf[i] = (uint8_t)(x & 0xFF);
	}
}

int stress_verify_pattern(const uint8_t *buf, uint32_t size,
			  uint32_t file_id, uint32_t offset)
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

/* =========================================================================
 * Filesystem operations
 * ========================================================================= */

static int do_create(uint32_t file_id, uint32_t *out_size)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);

	lfs_file_t file;
	int err = lfs_file_open(&s_lfs, &file, name,
				LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
	if (err < 0) return err;

	uint32_t total = MIN_FILE_SIZE +
		(stress_rand() % (MAX_FILE_SIZE - MIN_FILE_SIZE + 1));
	uint32_t written = 0;

	while (written < total) {
		uint32_t chunk = total - written;
		if (chunk > IO_CHUNK_SIZE) chunk = IO_CHUNK_SIZE;

		stress_fill_pattern(s_io_buf, chunk, file_id, written);
		lfs_ssize_t n = lfs_file_write(&s_lfs, &file, s_io_buf, chunk);
		if (n < 0) { lfs_file_close(&s_lfs, &file); return (int)n; }
		written += (uint32_t)n;
	}

	lfs_file_close(&s_lfs, &file);
	s_st.bytes_written += written;
	if (out_size) *out_size = written;
	return 0;
}

static int do_read_verify(uint32_t file_id, uint32_t expected_size)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);

	lfs_file_t file;
	int err = lfs_file_open(&s_lfs, &file, name, LFS_O_RDONLY);
	if (err < 0) return err;

	lfs_soff_t actual = lfs_file_size(&s_lfs, &file);
	if ((uint32_t)actual != expected_size) {
		lfs_file_close(&s_lfs, &file);
		return LFS_ERR_CORRUPT;
	}

	uint32_t offset = 0;
	while (offset < expected_size) {
		uint32_t chunk = expected_size - offset;
		if (chunk > IO_CHUNK_SIZE) chunk = IO_CHUNK_SIZE;

		lfs_ssize_t n = lfs_file_read(&s_lfs, &file, s_io_buf, chunk);
		if (n < 0) { lfs_file_close(&s_lfs, &file); return (int)n; }

		int bad = stress_verify_pattern(s_io_buf, (uint32_t)n,
						file_id, offset);
		if (bad >= 0) {
			lfs_file_close(&s_lfs, &file);
			return LFS_ERR_CORRUPT;
		}
		offset += (uint32_t)n;
		s_st.bytes_read += (uint32_t)n;
	}

	lfs_file_close(&s_lfs, &file);
	return 0;
}

static int do_delete(uint32_t file_id)
{
	char name[24];
	snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", file_id);
	return lfs_remove(&s_lfs, name);
}

/* =========================================================================
 * One randomised operation
 * ========================================================================= */

static void run_one_op(void)
{
	s_st.ops_total++;
	uint32_t r = stress_rand() % 100;

	/* 10 % — list */
	if (r < 10) {
		lfs_dir_t dir;
		if (lfs_dir_open(&s_lfs, &dir, "/") == LFS_ERR_OK) {
			struct lfs_info info;
			while (lfs_dir_read(&s_lfs, &dir, &info) > 0) {}
			lfs_dir_close(&s_lfs, &dir);
		}
		s_st.ops_list++;
		return;
	}
	r -= 10;

	/* 15 % — delete (if usage > target or random) */
	if (r < 15) {
		int pct = stress_fs_usage_pct();
		int idx = stress_files_random();
		if (idx >= 0 && (pct > TARGET_USAGE_PCT ||
				 (stress_rand() % 3) == 0)) {
			uint32_t id = s_files[idx].id;
			if (do_delete(id) == LFS_ERR_OK) {
				stress_files_del(id);
				s_st.ops_delete++;
				return;
			}
			s_st.fs_errors++;
		}
	}
	r -= 15;

	/* 40 % — read + verify */
	if (r < 40) {
		int idx = stress_files_random();
		if (idx >= 0) {
			if (do_read_verify(s_files[idx].id,
					   s_files[idx].size) == LFS_ERR_OK) {
				s_st.ops_read++;
				return;
			}
			s_st.verify_errors++;
			s_st.fs_errors++;
			return;
		}
	}
	r -= 40;

	/* 25 % — write (append) */
	if (r < 25) {
		int idx = stress_files_random();
		if (idx >= 0) {
			uint32_t id = s_files[idx].id;
			char name[24];
			snprintf(name, sizeof(name), "f%08" PRIx32 ".dat", id);
			lfs_file_t file;
			if (lfs_file_open(&s_lfs, &file, name,
					  LFS_O_WRONLY | LFS_O_APPEND) ==
			    LFS_ERR_OK) {
				uint32_t extra = (stress_rand() % 2048) + 128;
				uint32_t old  = s_files[idx].size;
				stress_fill_pattern(s_io_buf, extra, id, old);
				lfs_ssize_t n = lfs_file_write(&s_lfs, &file,
								s_io_buf, extra);
				lfs_file_close(&s_lfs, &file);
				if (n > 0) {
					s_files[idx].size += (uint32_t)n;
					s_st.bytes_written += (uint32_t)n;
					s_st.ops_write++;
					return;
				}
			}
			s_st.fs_errors++;
			return;
		}
	}

	/* fall-through — create if below target */
	if (stress_fs_usage_pct() < TARGET_USAGE_PCT &&
	    s_st.files_live < MAX_FILES) {
		uint32_t id = (stress_rand() & 0x7FFFFFFFu) | 1u;
		uint32_t sz = 0;
		if (do_create(id, &sz) == LFS_ERR_OK) {
			stress_files_add(id, sz);
			s_st.ops_create++;
			return;
		}
		s_st.fs_errors++;
		return;
	}

	/* nothing else — make room */
	if (s_st.files_live > 0) {
		int idx = stress_files_random();
		if (idx >= 0) {
			do_delete(s_files[idx].id);
			stress_files_del(s_files[idx].id);
			s_st.ops_delete++;
		}
	}
}

/* =========================================================================
 * Main loop
 * ========================================================================= */

void stress_run(void)
{
	printf("[3/3] Running");
	if (DURATION_HOURS > 0)
		printf(" for %d hours", DURATION_HOURS);
	else
		printf(" indefinitely");
	printf(" (status every %d min) ...\n\n", STATUS_INTERVAL_MIN);

	/* ---- init log on flash ---------------------------------------- */
	int err = stress_log_init();
	if (err != LFS_ERR_OK)
		printf("  WARNING: log file create FAIL (%s)\n",
		       stress_lfs_str(err));

	/* ---- run ------------------------------------------------------ */
	uint64_t t_start       = time_us_64();
	uint64_t t_next_status = t_start +
		(uint64_t)STATUS_INTERVAL_MIN * 60 * 1000000;
	uint64_t t_end = DURATION_HOURS > 0
		? t_start + (uint64_t)DURATION_HOURS * 3600 * 1000000
		: UINT64_MAX;

	while (1) {
		run_one_op();
		s_st.total_us = time_us_64() - t_start;

		if (s_st.total_us >= t_next_status) {
			stress_print_status();
			t_next_status += (uint64_t)STATUS_INTERVAL_MIN * 60 *
					 1000000;
		}

		if (s_st.total_us >= t_end) break;
		if (s_st.files_live > s_st.max_files_seen)
			s_st.max_files_seen = s_st.files_live;
	}

	/* ---- final summary -------------------------------------------- */
	{
		char buf[512];
		int n = snprintf(buf, sizeof(buf),
			"\n========================================\n"
			"  Test finished.\n\n"
			"  Duration:  %" PRIu32 " min\n"
			"  Total ops: %" PRIu32 "\n"
			"  Create: %" PRIu32 "  Write: %" PRIu32
			"  Read: %" PRIu32 "  Delete: %" PRIu32 "\n"
			"  Data written: %" PRIu32
			" KiB   read: %" PRIu32 " KiB\n"
			"  Peak files:   %" PRIu32 "\n",
			stress_elapsed_min(), s_st.ops_total,
			s_st.ops_create, s_st.ops_write,
			s_st.ops_read, s_st.ops_delete,
			s_st.bytes_written / 1024,
			s_st.bytes_read / 1024,
			s_st.max_files_seen);
		if (n > 0 && n < (int)sizeof(buf)) stress_log_write(buf);
	}

	if (s_st.verify_errors > 0 || s_st.fs_errors > 0) {
		char ebuf[64];
		int n = snprintf(ebuf, sizeof(ebuf),
			"  ERRORS — verify:%" PRIu32
			"  fs:%" PRIu32 "\n",
			s_st.verify_errors, s_st.fs_errors);
		if (n > 0 && n < (int)sizeof(ebuf)) stress_log_write(ebuf);
	} else {
		stress_log_write("  Errors: 0  —  stable.\n");
	}
	stress_log_write("========================================\n\n");

	stress_log_close();
}
