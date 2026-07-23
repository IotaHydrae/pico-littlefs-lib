# pico-littlefs

[中文版本](README_zh.md)

LittleFS port for the Raspberry Pi Pico (RP2040), built on the [Pico SDK](https://github.com/raspberrypi/pico-sdk).

Currently supports **SPI SD cards** as the storage backend, with a clean block-device abstraction that makes it straightforward to add other backends (e.g. SPI NOR flash, raw NAND, QSPI PSRAM).

## Features

- **LittleFS v2.x** — power-fail resilient, wear-levelled filesystem
- **SPI SD card** read / write via [`pico-sdcard-lib`](https://github.com/IotaHydrae/pico-sdcard-lib)
- **Pluggable backend** — add SPI flash or other storage without touching existing code
- **Zero heap allocation** — all buffers are statically allocated by the application
- **4 KiB blocks** tuned for SD card sector geometry

## Project Structure

```
pico-littlefs-lib/
├── CMakeLists.txt                # top-level build
├── lfs_bd.h                      # block-device abstraction
├── lfs_bd_sdcard.h               # SD card backend (public API)
├── lfs_bd_sdcard.c               # SD card backend (read / prog / erase / sync)
├── pico_littlefs.h               # optional convenience wrapper
├── pico_littlefs.c
├── sdcard_port_pico.h            # Pico pin & SPI config (macro overrides)
├── sdcard_port_pico.c            # hardware port (hardware_spi / hardware_gpio)
├── example/
│   ├── CMakeLists.txt
│   └── main.c                    # full-featured demo
└── lib/
    ├── littlefs/                 # upstream littlefs (git submodule)
    └── sdcard-lib/               # upstream SD card library (git submodule)
```

## Wiring

| Pico Pin | SD Card Pin | Signal | Notes |
|----------|-------------|--------|-------|
| GP16     | D0 / DO     | MISO   |       |
| GP17     | D3 / CS     | CS     | Manual GPIO (not hardware SPI CS) |
| GP18     | CLK / SCLK  | SCK    |       |
| GP19     | CMD / DI    | MOSI   |       |
| 3.3V OUT | VCC         | Power  | Do **not** use 5 V |
| GND      | GND         | Ground |       |

To change pin assignments, define the macros in `sdcard_port_pico.h` or pass them via compiler flags:

```c
#define SD_PORT_PICO_SPI      spi0
#define SD_PORT_PICO_PIN_MISO 16
#define SD_PORT_PICO_PIN_CS   17
#define SD_PORT_PICO_PIN_SCK  18
#define SD_PORT_PICO_PIN_MOSI 19
```

## Quick Start

### 1. Clone & pull submodules

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
sudo picotool load -fx ./example/pico-littlefs-demo.uf2
```

### 4. Monitor serial output

```bash
minicom -D /dev/ttyACM0
# or
cat /dev/ttyACM0
```

## Demo Output

With an SDHC card inserted, you should see output similar to:

```
========================================
  pico-littlefs Demo (SPI SD Card)
========================================

[1/5] Initialising SD card ...
  OK — SDHC/SDXC, SPI 8928571 Hz

[2/5] Configuring littlefs ...
  block_size:  4096 bytes
  block_count: 536870911
  cache_size:  4096 bytes
  read_size:   512 bytes
  prog_size:   512 bytes
  → filesystem capacity: 4194300 KiB

[3/5] Mounting filesystem ...
  Filesystem not found or corrupted — formatting ...
  Format OK.  Mounting again ...
  Mounted OK.

[4/5] File write / read test ...
  Wrote 90 bytes to 'hello.txt'
  Read 90 bytes: 'Hello from pico-littlefs on SPI SD card!
littlefs is a power-fail resilient filesystem.
'
  Appended 33 bytes.

  --- hello.txt contents ---
  File size: 123 bytes
  Hello from pico-littlefs on SPI SD card!
littlefs is a power-fail resilient filesystem.
Appended line — second write.
  --- end of file ---

[5/5] Listing root directory ...
  d        0  .
  d        0  ..
  f      123  hello.txt
  → 3 entries

----------------------------------------
Unmount: OK
========================================
  Demo complete.
========================================
```

## Architecture

The library enforces a clean separation between **filesystem logic**, **block-device I/O**, and **hardware access**:

```
your application
  │
  ├── lfs.h                     ← littlefs core API
  │     (format / mount / file I/O / directory)
  │
  ├── lfs_bd_sdcard.h           ← block-device backend
  │     implements lfs_config {read, prog, erase, sync}
  │     maps littlefs blocks → SD card sectors
  │
  └── sdcard.h / sdcard_port.h  ← SPI SD card protocol + port layer
        └── sdcard_port_pico.c  ← Pico GPIO / SPI implementation
```

`lfs_bd_sdcard.c` is a **concrete backend**. The `lfs_bd.h` header declares the generic abstraction — each backend is responsible for filling out a `struct lfs_config` with its own geometry and callbacks.

## Configuration

Default geometry (tuned for SD cards):

| Field | Value | Notes |
|-------|-------|-------|
| `read_size` | 512 | SD sector size |
| `prog_size` | 512 | SD sector size |
| `block_size` | 4096 | 8 sectors per littlefs block |
| `cache_size` | 4096 | one block cache |
| `lookahead_size` | 128 B | tracks 1024 blocks |
| `block_cycles` | 500 | wear-levelling threshold |

After calling `lfs_bd_sdcard_init()`, you can override any of these fields on the `struct lfs_config` before calling `lfs_format()` or `lfs_mount()`.

### Static Buffers

The example supplies its own read / program / lookahead buffers to avoid heap allocations:

```c
static uint8_t read_buffer[4096];
static uint8_t prog_buffer[4096];
static uint32_t lookahead_buffer[128 / sizeof(uint32_t)];

lfs_bd_sdcard_init(&cfg, &bd);
cfg.read_buffer      = read_buffer;
cfg.prog_buffer      = prog_buffer;
cfg.lookahead_buffer = lookahead_buffer;
```

## API Usage

Minimal example — format and mount a filesystem:

```c
#include "lfs.h"
#include "lfs_bd_sdcard.h"
#include "sdcard.h"

static lfs_bd_sdcard_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

int main(void) {
    stdio_init_all();

    // 1. Bring up the hardware
    sd_init();

    // 2. Wire littlefs to the SD card
    lfs_bd_sdcard_init(&cfg, &bd);

    // 3. Format (first use) or mount
    int err = lfs_mount(&lfs, &cfg);
    if (err == LFS_ERR_CORRUPT) {
        lfs_format(&lfs, &cfg);
        lfs_mount(&lfs, &cfg);
    }

    // 4. Use the filesystem
    lfs_file_t file;
    lfs_file_open(&lfs, &file, "data.bin",
                  LFS_O_WRONLY | LFS_O_CREAT);
    lfs_file_write(&lfs, &file, "hello", 5);
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
}
```

## Adding a New Backend

To support a different storage device (e.g. SPI NOR flash), create a new pair of files:

```
lfs_bd_spi_flash.h
lfs_bd_spi_flash.c
```

Implement these callbacks (the signatures come from `struct lfs_config`):

| Callback | Purpose |
|----------|---------|
| `read`  | Read `size` bytes from `block` at `off` into `buffer` |
| `prog`  | Program `size` bytes to `block` at `off` from `buffer` |
| `erase` | Erase `block` (can be no-op if the medium doesn't need it) |
| `sync`  | Flush caches (can be no-op for synchronous media) |

Provide an init function that fills out `struct lfs_config` with appropriate geometry and the callbacks above. No changes to `lfs_bd_sdcard.c`, `CMakeLists.txt`, or the example are required — just add your new source files and link them.

## Memory Footprint

```
   text      data       bss       dec
  78448         0     12224     90672   ( ≈ 88.5 KiB )
```

- **Flash**: ~77 KiB (littlefs core + SD library + demo app + Pico SDK)
- **RAM**:  ~12 KiB (filesystem state + cache buffers + SDK)

Well within the RP2040 2 MiB / 264 KiB budget.

## Dependencies

| Component | Source |
|-----------|--------|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | `$PICO_SDK_PATH` |
| [littlefs](https://github.com/littlefs-project/littlefs) | git submodule `lib/littlefs` |
| [pico-sdcard-lib](https://github.com/IotaHydrae/pico-sdcard-lib) | git submodule `lib/sdcard-lib` |

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE).
Copyright (c) 2025 Wooden Chair <hua.zheng@embeddedboys.com>.

LittleFS is copyright (c) 2022 The LittleFS Authors (BSD 3-Clause).
SD card library is copyright (c) 2025 Wooden Chair (MIT).
