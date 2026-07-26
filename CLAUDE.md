# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build, flash, monitor

```bash
cd build && cmake -G Ninja .. && ninja                     # build
sudo picotool load -fx ./examples/pico-littlefs-sdcard-demo.uf2   # flash (SD)
sudo picotool load -fx ./examples/pico-littlefs-flash-demo.uf2    # flash (NOR)
minicom -D /dev/ttyACM0                                    # serial monitor
```

`PICO_SDK_PATH` must be set. Build outputs `examples/pico-littlefs-sdcard-demo.{elf,uf2}`, `pico-littlefs-sdcard-large-read.{elf,uf2}`, and `pico-littlefs-flash-demo.{elf,uf2}`.

## Architecture — three strict layers

```
application (examples/sdcard_demo.c)
  │  calls lfs_file_* / lfs_dir_* / lfs_format / lfs_mount / lfs_unmount
  │
  ├── lfs.h  (littlefs core — lib/littlefs/)
  │     metadata, wear-levelling, power-fail resilience
  │
  ├── lfs_bd_sdcard.c  (block-device backend — this repo's main code)
  │     fills out struct lfs_config { read, prog, erase, sync, geometry }
  │     maps (lfs_block_t, lfs_off_t) → SD sector addresses
  │     calls sd_read_block() / sd_write_block() in 512-byte sector loop
  │
  └── sdcard.h / sdcard_port.h  (SPI SD protocol — lib/sdcard-lib/)
        sdcard.c: CMD framing, R1/R3/R7 parsing, CRC16, init sequence
        sdcard_port_pico.c: Pico hardware_spi / gpio / pico/time
```

**The boundary rule**: `lfs_bd_sdcard.c` knows about littlefs types *and* `sd_read_block`/`sd_write_block`. It must **not** include Pico SDK headers — hardware access goes through the sdcard port layer. `sdcard.c` is pure protocol — zero platform headers.

## Block-device geometry

| Field | Value | Why |
|-------|-------|-----|
| `read_size` | 512 | SD sector size — all reads are sector-aligned |
| `prog_size` | 512 | SD sector size — all writes are sector-aligned |
| `block_size` | 4096 | 8 sectors per littlefs erase block (balance metadata overhead vs RAM) |
| `cache_size` | 4096 | one block; caller provides static buffers |
| `lookahead_size` | 128 | tracks 1024 blocks in a compact bitmap |
| `block_cycles` | 500 | wear-levelling threshold for metadata relocation |

**erase callback is a no-op** — SD cards manage erase internally and don't require explicit erase-before-write. This is a conscious design choice (documented in `lfs_bd_sdcard.h`).

**sync callback is a no-op** — SD writes via the sdcard library are synchronous (the library polls until the card reports programming complete).

## littlefs block → SD sector mapping

```c
// In lfs_bd_sdcard.c:
sector = bd->sector_offset + (block * 4096 + off) / 512
```

Because `read_size`/`prog_size` = 512, littlefs guarantees all `off` and `size` values are multiples of 512. Each callback iterates `size / 512` sectors, calling `sd_read_block` / `sd_write_block` once per sector.

## Static buffer convention

The caller owns all RAM. The block-device init only fills geometry + callbacks; the application assigns `cfg.read_buffer`, `cfg.prog_buffer`, and `cfg.lookahead_buffer` after init. No heap allocations anywhere in the stack.

```c
static uint8_t read_buffer[4096];
static uint8_t prog_buffer[4096];
static uint32_t lookahead_buffer[128 / sizeof(uint32_t)];

lfs_bd_sdcard_init(&cfg, &bd);
cfg.read_buffer      = read_buffer;
cfg.prog_buffer      = prog_buffer;
cfg.lookahead_buffer = lookahead_buffer;
```

## Adding a new storage backend

The `lfs_bd.h` abstraction exists so backends can be swapped without touching the application or the filesystem logic:

1. Create `lfs_bd_<name>.h` / `lfs_bd_<name>.c`
2. Define a backend struct (stored in `lfs_config.context`)
3. Implement `read`, `prog`, `erase`, `sync` callbacks with your hardware
4. Provide an `lfs_bd_<name>_init(struct lfs_config *cfg, your_bd_t *bd)` that populates geometry and callbacks

No changes to the example, the CMakeLists, or existing backends are required. The difference from the SD card backend:
- Flash needs a real `erase` callback (not no-op)
- Flash may need `sync` if writes are buffered
- `block_size` should match the flash erase sector (typically 4 KiB for SPI NOR)

## CMake target layout

```
pico_littlefs_core        (STATIC) — lfs.c + lfs_util.c, no deps
pico_sdcard               (STATIC) — sdcard.c, from lib/sdcard-lib/
pico_spi_flash            (STATIC) — flash.c + flash_nor.c, from lib/spi-flash-lib/
pico_littlefs_bd          (STATIC) — lfs_bd_sdcard.c + lfs_bd_flash.c, links above
pico-littlefs-sdcard-demo (EXEC)   — examples/sdcard_demo.c + sdcard_port_pico.c
pico-littlefs-sdcard-large-read (EXEC) — examples/sdcard_large_read.c
pico-littlefs-flash-demo  (EXEC)   — examples/flash_demo.c + flash_port_pico.c
```

## Key dependencies

- `lib/littlefs/` — upstream littlefs v2.x (git submodule). Filesystem core; never modified.
- `lib/sdcard-lib/` — upstream SPI SD card library (git submodule). Protocol implementation; never modified.
- `sdcard_port_pico.c` — the **only** platform-specific file at this repo's level. Reuses the same port interface (`sdcard_port.h`) that all sdcard-lib consumers implement.
- `sdcard_port_pico.h` — pin/SPI macros (GP16-19, spi0). Users override these for custom wiring.

## Pin defaults

| Pico | SD card | Macro |
|------|---------|-------|
| GP16 | DO/MISO | `SD_PORT_PICO_PIN_MISO` |
| GP17 | CS | `SD_PORT_PICO_PIN_CS` |
| GP18 | SCK | `SD_PORT_PICO_PIN_SCK` |
| GP19 | DI/MOSI | `SD_PORT_PICO_PIN_MOSI` |

All in `sdcard_port_pico.h`. CS is manual GPIO, not hardware SPI CS — required by the SD SPI spec.
