/*
 * SD Card Stability Test
 *
 * Thin SD-specific layer on top of the shared stress-test
 * framework (stress_common.h).  Most logic lives in stress_common.c.
 *
 * WARNING: Destroys all data on the SD card — formats at startup.
 *
 * Flash:  sudo picotool load -fx ./tests/pico-sdcard-stress.uf2
 * Monitor: minicom -D /dev/ttyACM0
 */

/* ---- tunables (also settable via cmake -D) ---- */
#define DEVICE_NAME  "SPI SD Card"
#define CAPACITY_KIB s_sd_kib

/* ---- SD-specific includes ---- */
#include "sdcard.h"
#include "sdcard_port.h"
#include "sdcard_port_pico.h"
#include "lfs_bd_sdcard.h"

/* ---- shared framework (must come last — uses the macros above) ---- */
#include "stress_common.h"

/* =========================================================================
 * SD-specific state
 * ========================================================================= */

static lfs_bd_sdcard_t s_bd;
static uint32_t s_sd_kib;

static uint8_t s_read_buf[LFS_BD_SDCARD_DEFAULT_CACHE_SIZE];
static uint8_t s_prog_buf[LFS_BD_SDCARD_DEFAULT_CACHE_SIZE];
static uint32_t s_lookahead[256 / sizeof(uint32_t)];

struct lfs_config s_cfg;
lfs_t s_lfs;
uint8_t s_io_buf[MAX_FILE_SIZE];

/* =========================================================================
 * Callbacks required by stress_common.h
 * ========================================================================= */

int hw_init(void)
{
	int err = sd_init();
	if (err != SD_OK) return err;

	uint32_t sectors = sd_get_sector_count();
	s_sd_kib = (uint32_t)((uint64_t)sectors * 512 / 1024);
	return 0;
}

void hw_info(char *buf, size_t len)
{
	uint32_t sectors = sd_get_sector_count();
	snprintf(buf, len, "%" PRIu32 " KiB, %s, SPI %lu Hz",
		 s_sd_kib, sd_get_type() == SD_TYPE_SDHC ? "SDHC" : "SDSC",
		 (unsigned long)sd_get_baudrate());
}

int fs_init(void)
{
	int err = lfs_bd_sdcard_init(&s_cfg, &s_bd);
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
	printf("  Target usage: %d %%\n", TARGET_USAGE_PCT);
	printf("========================================\n\n");

	/* ---- 1. Init hardware ------------------------------------------- */
	printf("[1/3] Initialising %s ...\n", DEVICE_NAME);
	int err = hw_init();
	if (err) {
		printf("  FAIL: %s (code %d)\n", sd_error_str(err), err);

		printf("\n  --- SPI bus diagnostic ---\n");
		printf("  Pins: MISO=GP%d MOSI=GP%d SCK=GP%d CS=GP%d\n",
		       SD_PORT_PICO_PIN_MISO, SD_PORT_PICO_PIN_MOSI,
		       SD_PORT_PICO_PIN_SCK, SD_PORT_PICO_PIN_CS);
		printf("  Sending 16 dummy clocks (CS high) ...\n  ");
		for (int i = 0; i < 16; i++)
			printf("%02X ", sd_port_spi_rw(0xFF));
		printf("\n");
		while (1) sleep_ms(1000);
	}

	char info[80];
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
