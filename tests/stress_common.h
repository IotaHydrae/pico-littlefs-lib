/*
 * Stress Test Common Framework
 *
 * Shared logic for stability tests across all storage backends.
 * Include this header from a backend-specific test file and
 * provide your own hw_init() and fs_init() implementations.
 *
 * Required before #include:
 *   - HW_INIT()       — hardware init, returns 0 on success
 *   - FS_INIT()       — format + mount, returns 0 on success
 *   - FS_UNMOUNT()    — unmount the filesystem
 *   - CAPACITY_KIB    — device capacity for banner
 *   - DEVICE_NAME     — human-readable name for banner
 *
 * Optional (with defaults):
 *   - DURATION_HOURS       (24)
 *   - STATUS_INTERVAL_MIN  (30)
 *   - TARGET_USAGE_PCT     (70)
 *   - MAX_FILES            (128)
 *   - MIN_FILE_SIZE        (256)
 *   - MAX_FILE_SIZE        (128*1024)
 */

#ifndef STRESS_COMMON_H
#define STRESS_COMMON_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lfs.h"

/* =========================================================================
 * Tunables
 * ========================================================================= */

#ifndef DURATION_HOURS
#define DURATION_HOURS 24
#endif
#ifndef STATUS_INTERVAL_MIN
#define STATUS_INTERVAL_MIN 30
#endif
#ifndef TARGET_USAGE_PCT
#define TARGET_USAGE_PCT 70
#endif
#ifndef MAX_FILES
#define MAX_FILES 128
#endif
#ifndef MIN_FILE_SIZE
#define MIN_FILE_SIZE 256
#endif
#ifndef MAX_FILE_SIZE
#define MAX_FILE_SIZE (128 * 1024)
#endif
#ifndef IO_CHUNK_SIZE
#define IO_CHUNK_SIZE 4096
#endif

/* =========================================================================
 * Global state — declared extern, defined exactly once by the includer
 * ========================================================================= */

extern struct lfs_config s_cfg;
extern lfs_t s_lfs;
extern uint8_t s_io_buf[MAX_FILE_SIZE];

/* =========================================================================
 * Statistics
 * ========================================================================= */

typedef struct {
	uint32_t ops_total;
	uint32_t ops_create, ops_write, ops_read, ops_delete, ops_list;
	uint32_t bytes_written, bytes_read;
	uint32_t verify_errors, fs_errors;
	uint32_t files_live, max_files_seen;
	uint64_t total_us;
} stress_stats_t;

extern stress_stats_t s_st;

/* =========================================================================
 * Logging
 * ========================================================================= */

#define LOG_DIR  "/var"
#define LOG_FILE LOG_DIR "/stress_test_" __DATE__ ".log"

extern lfs_file_t s_log_file;
extern bool s_log_open;

int stress_log_init(void);
void stress_log_write(const char *msg);
void stress_log_close(void);

/* =========================================================================
 * Helpers
 * ========================================================================= */

const char *stress_lfs_str(int err);
uint32_t stress_elapsed_min(void);
int stress_fs_usage_pct(void);
void stress_print_status(void);

/* =========================================================================
 * File tracking
 * ========================================================================= */

void stress_files_add(uint32_t id, uint32_t size);
void stress_files_del(uint32_t id);
int  stress_files_random(void);   /* returns index, -1 if empty */

/* =========================================================================
 * Data pattern helpers
 * ========================================================================= */

void stress_fill_pattern(uint8_t *buf, uint32_t size,
			 uint32_t file_id, uint32_t offset);
int  stress_verify_pattern(const uint8_t *buf, uint32_t size,
			   uint32_t file_id, uint32_t offset);
uint32_t stress_rand(void);

/* =========================================================================
 * User-provided callbacks
 * ========================================================================= */

int  hw_init(void);       /* initialise storage hardware, return 0 on success */
int  fs_init(void);       /* format + mount littlefs, return 0 on success    */
void fs_unmount(void);    /* unmount littlefs                                */
void hw_info(char *buf, size_t len);  /* one-line hardware info for banner   */

/* =========================================================================
 * Main loop — call from main()
 * ========================================================================= */

void stress_run(void);

#endif /* STRESS_COMMON_H */
