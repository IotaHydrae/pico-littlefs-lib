/*
 * pico-littlefs Demo — Raspberry Pi Pico + SPI SD Card
 *
 * Exercises littlefs on an SPI-attached SD card:
 *   1. Initialise SD card
 *   2. Format with littlefs (DESTRUCTIVE — skips if already formatted)
 *   3. Mount
 *   4. Create a file, write to it, read it back
 *   5. List root directory
 *   6. Unmount
 *
 * Build:
 *   mkdir -p build && cd build && cmake .. && ninja
 *
 * Flash:
 *   sudo picotool load -fx ./example/pico-littlefs-demo.uf2
 *
 * Monitor:
 *   minicom -D /dev/ttyACM0    or    cat /dev/ttyACM0
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lfs.h"
#include "lfs_bd_sdcard.h"
#include "sdcard.h"
#include "sdcard_port.h"
#include "sdcard_port_pico.h"

/* =========================================================================
 * Static buffers — avoid malloc in embedded context
 * ========================================================================= */

static lfs_bd_sdcard_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

/* littlefs cache buffers (4 KiB each = 1 block) */
static uint8_t read_buffer[4096];
static uint8_t prog_buffer[4096];
static uint32_t lookahead_buffer[128 / sizeof(uint32_t)];

/* =========================================================================
 * Helpers
 * ========================================================================= */

static const char *lfs_err_str(int err) {
    switch (err) {
    case LFS_ERR_OK:          return "OK";
    case LFS_ERR_IO:          return "IO error";
    case LFS_ERR_CORRUPT:     return "Corrupted";
    case LFS_ERR_NOENT:       return "No such entry";
    case LFS_ERR_EXIST:       return "Already exists";
    case LFS_ERR_NOTDIR:      return "Not a directory";
    case LFS_ERR_ISDIR:       return "Is a directory";
    case LFS_ERR_NOTEMPTY:    return "Directory not empty";
    case LFS_ERR_BADF:        return "Bad file descriptor";
    case LFS_ERR_FBIG:        return "File too large";
    case LFS_ERR_INVAL:       return "Invalid parameter";
    case LFS_ERR_NOSPC:       return "No space left";
    case LFS_ERR_NOMEM:       return "No memory";
    case LFS_ERR_NAMETOOLONG: return "Name too long";
    default:                  return "Unknown";
    }
}

static const char *card_type_str(sd_card_type_t t) {
    switch (t) {
    case SD_TYPE_SDSC: return "SDSC";
    case SD_TYPE_SDHC: return "SDHC/SDXC";
    default:           return "Unknown";
    }
}

static void print_divider(void) {
    printf("----------------------------------------\n");
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    stdio_init_all();
    sleep_ms(2000);   /* wait for USB serial to enumerate */

    printf("\n");
    printf("========================================\n");
    printf("  pico-littlefs Demo (SPI SD Card)\n");
    printf("========================================\n\n");

    /* ---- 1. Initialise SD card ---------------------------------------- */
    printf("[1/5] Initialising SD card ...\n");
    int err = sd_init();
    if (err != SD_OK) {
        printf("  FAIL: %s (code %d)\n", sd_error_str(err), err);

        /* ---- diagnostic: check if anything is on the SPI bus --------- */
        printf("\n  --- SPI bus diagnostic ---\n");
        printf("  Pins: MISO=GP%d MOSI=GP%d SCK=GP%d CS=GP%d\n",
               SD_PORT_PICO_PIN_MISO, SD_PORT_PICO_PIN_MOSI,
               SD_PORT_PICO_PIN_SCK,  SD_PORT_PICO_PIN_CS);

        /* Send dummy clocks with CS high — MISO should read 0xFF if
         * the card is present (DO held high by pull-up when idle).
         * If MISO is always 0x00 there may be a short to GND.
         * If MISO == MOSI (loopback), MISO and MOSI are bridged. */
        printf("  Sending 16 dummy clocks (CS high) ...\n  ");
        for (int i = 0; i < 16; i++) {
            uint8_t b = sd_port_spi_rw(0xFF);
            printf("%02X ", b);
        }
        printf("\n  (all 0xFF = bus idle / no card; "
               "all 0x00 = possible short; "
               "other = unexpected)\n");

        printf("\n  Troubleshooting:\n");
        printf("  - Is the SD card fully inserted?\n");
        printf("  - Is the card formatted? (try another card)\n");
        printf("  - Check 3.3V power (not 5V!) to the SD card\n");
        printf("  - Verify wiring matches the pins above\n");
        printf("  - Try the sdcard-lib's own demo to isolate:\n");
        printf("    sudo picotool load -fx "
               "./sdcard_lib_build/examples/pico/pico-sdcard-demo.uf2\n");
        while (1) sleep_ms(1000);
    }
    printf("  OK — %s, SPI %lu Hz\n\n",
           card_type_str(sd_get_type()),
           (unsigned long)sd_get_baudrate());

    /* ---- 2. Configure littlefs block device --------------------------- */
    printf("[2/5] Configuring littlefs ...\n");

    /* Default: use entire card, 4 KiB blocks */
    err = lfs_bd_sdcard_init(&cfg, &bd);
    if (err != LFS_ERR_OK) {
        printf("  FAIL: %s (code %d)\n", lfs_err_str(err), err);
        while (1) sleep_ms(1000);
    }

    /* Supply static buffers (no heap allocations) */
    cfg.read_buffer      = read_buffer;
    cfg.prog_buffer      = prog_buffer;
    cfg.lookahead_buffer = lookahead_buffer;

    printf("  block_size:  %lu bytes\n", (unsigned long)cfg.block_size);
    printf("  block_count: %lu\n",       (unsigned long)cfg.block_count);
    printf("  cache_size:  %lu bytes\n", (unsigned long)cfg.cache_size);
    printf("  read_size:   %lu bytes\n", (unsigned long)cfg.read_size);
    printf("  prog_size:   %lu bytes\n", (unsigned long)cfg.prog_size);
    printf("  → filesystem capacity: %lu KiB\n\n",
           (unsigned long)(cfg.block_count * cfg.block_size / 1024));

    /* ---- 3. Mount (format if needed) ---------------------------------- */
    printf("[3/5] Mounting filesystem ...\n");

    /* Try auto-detect first (handles PC-formatted cards with block_size=512
     * and Pico-formatted cards with block_size=4096 seamlessly). */
    err = lfs_bd_sdcard_mount_auto(&lfs, &cfg, &bd);

    if (err == LFS_ERR_CORRUPT || err == LFS_ERR_INVAL) {
        if (err == LFS_ERR_INVAL)
            printf("  No matching geometry found — formatting ...\n");
        else
            printf("  Filesystem not found or corrupted — formatting ...\n");

        /* Reset to our preferred defaults before formatting */
        cfg.block_size  = 512;
        cfg.block_count = sd_get_sector_count() / (512 / 512);
        cfg.cache_size  = 512;

        err = lfs_format(&lfs, &cfg);
        if (err != LFS_ERR_OK) {
            printf("  Format FAIL: %s (code %d)\n", lfs_err_str(err), err);
            while (1) sleep_ms(1000);
        }
        printf("  Format OK (block_size=%lu, block_count=%lu).\n",
               (unsigned long)cfg.block_size, (unsigned long)cfg.block_count);
        printf("  Mounting again ...\n");
        err = lfs_mount(&lfs, &cfg);
    }

    if (err != LFS_ERR_OK) {
        printf("  Mount FAIL: %s (code %d)\n", lfs_err_str(err), err);
        while (1) sleep_ms(1000);
    }
    printf("  Mounted OK (block_size=%lu, block_count=%lu).\n\n",
           (unsigned long)cfg.block_size, (unsigned long)cfg.block_count);

    /* ---- 4. File write + read test ------------------------------------ */
    printf("[4/5] File write / read test ...\n");

    const char *filename = "hello.txt";
    const char *message  = "Hello from pico-littlefs on SPI SD card!\r\n"
                           "littlefs is a power-fail resilient filesystem.\r\n";

    /* -- 4a. Write -------------------------------------------------- */
    lfs_file_t file;
    err = lfs_file_open(&lfs, &file, filename,
                        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != LFS_ERR_OK) {
        printf("  lfs_file_open(w) FAIL: %s\n", lfs_err_str(err));
        while (1) sleep_ms(1000);
    }

    lfs_size_t wrote = lfs_file_write(&lfs, &file, message, strlen(message));
    if (wrote != strlen(message)) {
        printf("  Write FAIL: %lu / %zu bytes\n",
               (unsigned long)wrote, strlen(message));
        lfs_file_close(&lfs, &file);
        while (1) sleep_ms(1000);
    }
    printf("  Wrote %lu bytes to '%s'\n",
           (unsigned long)wrote, filename);

    err = lfs_file_close(&lfs, &file);
    if (err != LFS_ERR_OK)
        printf("  Close(w) FAIL: %s\n", lfs_err_str(err));

    /* -- 4b. Read back ---------------------------------------------- */
    char readback[256];
    memset(readback, 0, sizeof(readback));

    err = lfs_file_open(&lfs, &file, filename, LFS_O_RDONLY);
    if (err != LFS_ERR_OK) {
        printf("  lfs_file_open(r) FAIL: %s\n", lfs_err_str(err));
        while (1) sleep_ms(1000);
    }

    lfs_ssize_t nread = lfs_file_read(&lfs, &file, readback, sizeof(readback) - 1);
    if (nread < 0) {
        printf("  Read FAIL: %s\n", lfs_err_str((int)nread));
        lfs_file_close(&lfs, &file);
        while (1) sleep_ms(1000);
    }
    readback[nread] = '\0';
    printf("  Read %ld bytes: '%s'\n", (long)nread, readback);

    err = lfs_file_close(&lfs, &file);
    if (err != LFS_ERR_OK)
        printf("  Close(r) FAIL: %s\n", lfs_err_str(err));

    /* -- 4c. Append test -------------------------------------------- */
    const char *append_msg = "Appended line — second write.\r\n";
    err = lfs_file_open(&lfs, &file, filename,
                        LFS_O_WRONLY | LFS_O_APPEND);
    if (err != LFS_ERR_OK) {
        printf("  Append open FAIL: %s\n", lfs_err_str(err));
    } else {
        wrote = lfs_file_write(&lfs, &file, append_msg, strlen(append_msg));
        printf("  Appended %lu bytes.\n", (unsigned long)wrote);
        lfs_file_close(&lfs, &file);
    }

    /* -- 4d. Read and print full file ------------------------------- */
    printf("\n  --- %s contents ---\n", filename);
    err = lfs_file_open(&lfs, &file, filename, LFS_O_RDONLY);
    if (err == LFS_ERR_OK) {
        lfs_soff_t sz = lfs_file_size(&lfs, &file);
        printf("  File size: %ld bytes\n", (long)sz);
        char buf[64];
        lfs_ssize_t rd;
        while ((rd = lfs_file_read(&lfs, &file, buf, sizeof(buf) - 1)) > 0) {
            buf[rd] = '\0';
            printf("  %s", buf);
        }
        if (!strchr(readback, '\n'))
            printf("\n");
        lfs_file_close(&lfs, &file);
    }
    printf("  --- end of file ---\n\n");

    /* ---- 5. List root directory --------------------------------------- */
    printf("[5/5] Listing root directory ...\n");

    lfs_dir_t dir;
    err = lfs_dir_open(&lfs, &dir, "/");
    if (err != LFS_ERR_OK) {
        printf("  lfs_dir_open FAIL: %s\n", lfs_err_str(err));
    } else {
        struct lfs_info info;
        int count = 0;
        while (lfs_dir_read(&lfs, &dir, &info) > 0) {
            printf("  %c %8ld  %s\n",
                   info.type == LFS_TYPE_DIR ? 'd' : 'f',
                   (long)info.size,
                   info.name);
            count++;
        }
        printf("  → %d entries\n", count);
        lfs_dir_close(&lfs, &dir);
    }

    printf("\n");
    print_divider();

    /* ---- 6. Unmount --------------------------------------------------- */
    err = lfs_unmount(&lfs);
    printf("Unmount: %s\n", lfs_err_str(err));

    printf("========================================\n");
    printf("  Demo complete.\n");
    printf("========================================\n\n");

    for (;;) sleep_ms(1000);
}
