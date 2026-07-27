/*
 * SPI Flash Stability Test
 *
 * Thin flash-specific layer on top of the shared stress-test
 * framework (stress_common.h).  Most logic lives in stress_common.c.
 *
 * Flash:  sudo picotool load -fx ./tests/pico-flash-stress.uf2
 * Monitor: minicom -D /dev/ttyACM0
 */

/* ---- tunables (also settable via cmake -D) ---- */
#define DEVICE_NAME  "SPI NOR Flash"
#define CAPACITY_KIB s_flash_kib

/* ---- flash-specific includes ---- */
#include "flash.h"
#include "lfs_bd_flash.h"

/* ---- shared framework (must come last — uses the macros above) ---- */
#include "stress_common.h"

/* =========================================================================
 * Flash-specific state
 * ========================================================================= */

static lfs_bd_flash_t s_bd;
static uint32_t s_flash_kib;

/* buffers must be sized to match block_size */
static uint8_t s_read_buf[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint8_t s_prog_buf[LFS_BD_FLASH_DEFAULT_CACHE_SIZE];
static uint32_t s_lookahead[256 / sizeof(uint32_t)];

struct lfs_config s_cfg;
lfs_t s_lfs;
uint8_t s_io_buf[MAX_FILE_SIZE];

/* =========================================================================
 * Callbacks required by stress_common.h
 * ========================================================================= */

int hw_init(void)
{
	int err = spi_flash_init();
	if (err != SPI_FLASH_OK) return err;

	spi_flash_info_t info;
	spi_flash_get_info(&info);
	s_flash_kib = info.size_bytes / 1024;
	return 0;
}

void hw_info(char *buf, size_t len)
{
	spi_flash_info_t info;
	spi_flash_get_info(&info);
	snprintf(buf, len, "%" PRIu32 " KiB, mfr=0x%02X",
		 s_flash_kib, info.manufacturer_id);
}

int fs_init(void)
{
	int err = lfs_bd_flash_init(&s_cfg, &s_bd);
	if (err != LFS_ERR_OK) return err;

	s_cfg.read_buffer      = s_read_buf;
	s_cfg.prog_buffer      = s_prog_buf;
	s_cfg.lookahead_buffer = s_lookahead;

	err = lfs_format(&s_lfs, &s_cfg);
	if (err != LFS_ERR_OK) return err;

	return lfs_mount(&s_lfs, &s_cfg);
}

void fs_unmount(void)
{
	lfs_unmount(&s_lfs);
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
	printf("  %s 24-Hour Stability Test\n", DEVICE_NAME);
	printf("  Duration: %d h  |  Max files: %d\n",
	       DURATION_HOURS, MAX_FILES);
	printf("  File sizes: %d B – %d KiB\n",
	       MIN_FILE_SIZE, MAX_FILE_SIZE / 1024);
	printf("  Target flash usage: %d %%\n", TARGET_USAGE_PCT);
	printf("========================================\n\n");

	/* ---- 1. Init hardware ------------------------------------------- */
	printf("[1/3] Initialising %s ...\n", DEVICE_NAME);
	int err = hw_init();
	if (err) {
		printf("  FAIL (code %d)\n", err);
		while (1) sleep_ms(1000);
	}

	char info[64];
	hw_info(info, sizeof(info));
	printf("  OK — %s\n\n", info);

	/* ---- 2. Format + mount ------------------------------------------ */
	printf("[2/3] Formatting filesystem ...\n");
	err = fs_init();
	if (err) {
		printf("  FAIL: %s\n", stress_lfs_str(err));
		while (1) sleep_ms(1000);
	}
	printf("  Ready — %" PRIu32 " blocks, %" PRIu32 " KiB capacity\n\n",
	       s_cfg.block_count,
	       (uint32_t)((uint64_t)s_cfg.block_count *
			  s_cfg.block_size / 1024));

	/* ---- 3. Run shared stress loop ---------------------------------- */
	stress_run();

	/* ---- Cleanup ---------------------------------------------------- */
	fs_unmount();

	for (;;) sleep_ms(1000);
}
