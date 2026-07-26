# pico-littlefs

[中文版本](README_zh.md)

LittleFS port for the Raspberry Pi Pico (RP2040), built on the [Pico SDK](https://github.com/raspberrypi/pico-sdk).

Supports **SPI SD cards** and **SPI NOR flash** through a clean block-device abstraction — swap backends without touching application code.

## Features

- **LittleFS v2.x** — power-fail resilient, wear-levelled filesystem
- **SPI SD card** backend via [`pico-sdcard-lib`](https://github.com/IotaHydrae/pico-sdcard-lib)
- **SPI NOR flash** backend via [`pico-spi-flash-lib`](https://github.com/IotaHydrae/pico-spi-flash-lib)
- **Pluggable backends** — add new storage without modifying existing code
- **Zero heap allocation** — all buffers are statically allocated
- **Automatic geometry detection** — mount PC-formatted cards seamlessly

## Project Structure

```
pico-littlefs-lib/
├── CMakeLists.txt                  # top-level build
├── lfs_bd.h                        # block-device abstraction
├── lfs_bd_sdcard.h / .c            # SD card backend
├── lfs_bd_flash.h / .c             # SPI NOR flash backend
├── sdcard_port_pico.h / .c         # SD card hardware port
├── flash_port_pico.h               # flash pin config
├── pico_littlefs.h / .c            # optional convenience wrapper
├── examples/
│   ├── sdcard_demo.c               # SD card demo
│   ├── sdcard_large_read.c         # SD card large-file read benchmark
│   ├── flash_demo.c                # SPI flash demo
│   └── CMakeLists.txt
└── lib/
    ├── littlefs/                   # upstream littlefs (git submodule)
    ├── sdcard-lib/                 # SD card library (git submodule)
    └── spi-flash-lib/              # SPI flash library (git submodule)
```

## Quick Start

### 1. Clone

```bash
git clone --recurse-submodules https://github.com/IotaHydrae/pico-littlefs-lib.git
cd pico-littlefs-lib
```

### 2. Build

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

Requires `PICO_SDK_PATH` to point to a working Pico SDK checkout.

### 3. Flash

```bash
# SD card demo
sudo picotool load -fx ./examples/pico-littlefs-sdcard-demo.uf2

# SD card large-file read benchmark
sudo picotool load -fx ./examples/pico-littlefs-sdcard-large-read.uf2

# SPI flash demo
sudo picotool load -fx ./examples/pico-littlefs-flash-demo.uf2
```

### 4. Monitor

```bash
minicom -D /dev/ttyACM0
```

## Wiring

### SD Card (SPI0: GP16–19)

| Pico Pin | SD Card Pin | Signal |
|----------|-------------|--------|
| GP16     | D0 / DO     | MISO   |
| GP17     | D3 / CS     | CS     |
| GP18     | CLK / SCLK  | SCK    |
| GP19     | CMD / DI    | MOSI   |
| 3.3V OUT | VCC         | Power  |
| GND      | GND         | Ground |

### SPI Flash (SPI0: GP16–19, same bus OK with separate CS)

| Pico Pin | Flash Pin  | Signal |
|----------|-------------|--------|
| GP16     | DI / IO1    | MISO   |
| GP17     | /CS         | CS     |
| GP18     | CLK         | SCK    |
| GP19     | DO / IO0    | MOSI   |
| 3.3V OUT | VCC         | Power  |
| GND      | GND         | Ground |

Override pin assignments in `sdcard_port_pico.h` / `flash_port_pico.h`.

## Architecture

```
your application
  │
  ├── lfs.h                          ← littlefs core API
  │
  ├── lfs_bd_sdcard.c                ← SD card backend
  │     implements {read, prog, erase, sync}
  │     maps littlefs blocks → SD sectors
  │     calls sd_read_blocks() (CMD18 multi-block)
  │
  ├── lfs_bd_flash.c                 ← SPI flash backend
  │     implements {read, prog, erase, sync}
  │     maps littlefs blocks → byte addresses
  │     calls spi_flash_read/write/erase_sector()
  │
  └── sdcard.h / flash.h             ← hardware libraries
        └── *_port_pico.c            ← Pico SPI / GPIO
```

## Backend Comparison

| | SD Card | SPI NOR Flash |
|---|---|---|
| **Default block_size** | 16384 (32 sectors) | 4096 (sector size) |
| **read/prog alignment** | 512 bytes | 1 byte |
| **Erase** | no-op (card-managed) | `spi_flash_erase_sector()` |
| **Read speed** | ~600 KiB/s (SPI) | ~1 MiB/s (SPI) |
| **Port file** | `sdcard_port_pico.c` | `flash_port_pico.c` |

## Configuration

Default geometry (override via compiler flags `-DLFS_BD_SDCARD_DEFAULT_BLOCK_SIZE=...`):

| Field | SD Card | Flash |
|-------|---------|-------|
| `block_size` | 16384 | 4096 |
| `cache_size` | 16384 | 4096 |
| `read_size` | 512 | 1 |
| `prog_size` | 512 | 1 |
| `lookahead_size` | 256 B | 256 B |
| `block_cycles` | 500 | 500 |

### Static Buffers

```c
// Buffer sizes track the macro — change once in the header:
static uint8_t read_buffer[LFS_BD_SDCARD_DEFAULT_CACHE_SIZE];
static uint8_t prog_buffer[LFS_BD_SDCARD_DEFAULT_CACHE_SIZE];
static uint32_t lookahead_buffer[256 / sizeof(uint32_t)];

lfs_bd_sdcard_init(&cfg, &bd);
cfg.read_buffer      = read_buffer;
cfg.prog_buffer      = prog_buffer;
cfg.lookahead_buffer = lookahead_buffer;
```

## API Usage

```c
#include "lfs.h"
#include "lfs_bd_sdcard.h"  // or lfs_bd_flash.h
#include "sdcard.h"          // or flash.h

static lfs_bd_sdcard_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

int main(void) {
    stdio_init_all();
    sd_init();                           // 1. init hardware
    lfs_bd_sdcard_init(&cfg, &bd);       // 2. wire littlefs

    int err = lfs_mount(&lfs, &cfg);     // 3. mount
    if (err == LFS_ERR_CORRUPT) {
        lfs_format(&lfs, &cfg);          //    format if needed
        lfs_mount(&lfs, &cfg);
    }

    lfs_file_t file;                     // 4. use
    lfs_file_open(&lfs, &file, "data.bin",
                  LFS_O_WRONLY | LFS_O_CREAT);
    lfs_file_write(&lfs, &file, "hello", 5);
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
}
```

## Porting to a New Storage Device

Adding a new backend (e.g. NAND flash, eMMC, PSRAM) requires two files and zero changes to existing code:

### 1. Create the backend header — `lfs_bd_<name>.h`

```c
#ifndef LFS_BD_MYDEVICE_H
#define LFS_BD_MYDEVICE_H

#include "lfs.h"
#include <stdint.h>

#ifndef LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE
#define LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE 4096
#endif

typedef struct lfs_bd_mydevice {
    uint32_t size_limit;    /* 0 = auto-detect */
    uint32_t byte_offset;   /* usually 0 */
} lfs_bd_mydevice_t;

int lfs_bd_mydevice_init(struct lfs_config *cfg, lfs_bd_mydevice_t *bd);

#endif
```

### 2. Implement the four callbacks — `lfs_bd_<name>.c`

```c
#include "lfs_bd_mydevice.h"
#include "mydevice.h"   /* your hardware driver */

static int bd_mydevice_read(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, void *buffer, lfs_size_t size)
{
    lfs_bd_mydevice_t *bd = (lfs_bd_mydevice_t *)c->context;
    uint32_t addr = bd->byte_offset + block * c->block_size + off;
    return mydevice_read(addr, buffer, size) == OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int bd_mydevice_prog(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, const void *buffer, lfs_size_t size)
{
    /* ... same pattern as read, but write ... */
    return LFS_ERR_OK;
}

static int bd_mydevice_erase(const struct lfs_config *c, lfs_block_t block)
{
    /* Flash: call hardware erase.  SD/eMMC: no-op. */
    return LFS_ERR_OK;
}

static int bd_mydevice_sync(const struct lfs_config *c)
{
    /* Flush caches if your writes are buffered, else no-op. */
    return LFS_ERR_OK;
}

int lfs_bd_mydevice_init(struct lfs_config *cfg, lfs_bd_mydevice_t *bd)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->read_size  = 1;         /* byte- or sector-aligned */
    cfg->prog_size  = 1;         /* as above */
    cfg->block_size = LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE;
    cfg->block_count = mydevice_total_bytes() / cfg->block_size;
    cfg->block_cycles = 500;
    cfg->cache_size = LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE;
    cfg->lookahead_size = 256;

    cfg->context = bd;
    cfg->read  = bd_mydevice_read;
    cfg->prog  = bd_mydevice_prog;
    cfg->erase = bd_mydevice_erase;
    cfg->sync  = bd_mydevice_sync;
    return LFS_ERR_OK;
}
```

### 3. Hook into the build

Add your `.c` to `pico_littlefs_bd` in the top-level `CMakeLists.txt`:

```cmake
target_sources(pico_littlefs_bd PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lfs_bd_mydevice.c)
```

### Key decisions per storage type

| | NOR Flash | SD/eMMC | NAND Flash |
|---|---|---|---|
| **read_size** | 1 (byte) | 512 (sector) | page size |
| **prog_size** | 1 (byte) | 512 (sector) | page size |
| **erase** | real | no-op | real |
| **sync** | no-op | no-op | flush cache |
| **block_size** | 4096 (sector) | 16384 (perf) | erase block |
| **block_count** | total / block_size | sectors / (bs/512) | total / block_size |

### Reference implementations

- `lfs_bd_sdcard.c` — 512-byte sector I/O, CMD18 multi-block read, no-op erase
- `lfs_bd_flash.c` — byte-level I/O, real erase, no-op sync

## PC Cross-Mounting (SD Card)

```
Pico-formatted → PC:  ./lfs --block_size=16384 /dev/sda mount
PC-formatted   → Pico: auto-detected by lfs_bd_sdcard_mount_auto()
```

## Dependencies

| Component | Source |
|-----------|--------|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | `$PICO_SDK_PATH` |
| [littlefs](https://github.com/littlefs-project/littlefs) | `lib/littlefs` |
| [pico-sdcard-lib](https://github.com/IotaHydrae/pico-sdcard-lib) | `lib/sdcard-lib` |
| [pico-spi-flash-lib](https://github.com/IotaHydrae/pico-spi-flash-lib) | `lib/spi-flash-lib` |

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2025 Wooden Chair &lt;hua.zheng@embeddedboys.com&gt;.

LittleFS is copyright (c) 2022 The LittleFS Authors (BSD 3-Clause).
SD card library is copyright (c) 2025 Wooden Chair (MIT).
SPI flash library is copyright (c) 2025 Wooden Chair (MIT).
