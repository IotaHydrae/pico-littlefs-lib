# pico-littlefs

[English](README.md)

基于 [Pico SDK](https://github.com/raspberrypi/pico-sdk) 的 Raspberry Pi Pico (RP2040) LittleFS 移植。

目前支持 **SPI SD 卡** 作为存储后端，并提供清晰的块设备抽象层，便于后续添加其他后端（如 SPI NOR Flash、NAND、QSPI PSRAM 等）。

## 特性

- **LittleFS v2.x** — 掉电保护、磨损均衡的文件系统
- **SPI SD 卡** 读写，基于 [`pico-sdcard-lib`](https://github.com/IotaHydrae/pico-sdcard-lib)
- **可插拔后端** — 无需修改现有代码即可添加 SPI Flash 等新存储介质
- **零堆内存分配** — 所有缓冲区由应用程序静态分配
- **4 KiB 块大小**，针对 SD 卡扇区几何优化

## 项目结构

```
pico-littlefs-lib/
├── CMakeLists.txt                # 顶层构建
├── lfs_bd.h                      # 块设备抽象接口
├── lfs_bd_sdcard.h               # SD 卡后端（公开 API）
├── lfs_bd_sdcard.c               # SD 卡后端实现（read / prog / erase / sync）
├── pico_littlefs.h               # 可选的便利封装
├── pico_littlefs.c
├── sdcard_port_pico.h            # Pico 引脚与 SPI 配置（宏覆写）
├── sdcard_port_pico.c            # 硬件端口实现（hardware_spi / hardware_gpio）
├── example/
│   ├── CMakeLists.txt
│   └── main.c                    # 完整演示程序
└── lib/
    ├── littlefs/                 # littlefs 上游源码（git submodule）
    └── sdcard-lib/               # SD 卡库上游源码（git submodule）
```

## 接线

| Pico 引脚 | SD 卡引脚  | 信号   | 备注 |
|-----------|------------|--------|------|
| GP16      | D0 / DO    | MISO   |      |
| GP17      | D3 / CS    | CS     | 手动 GPIO，非硬件 SPI CS |
| GP18      | CLK / SCLK | SCK    |      |
| GP19      | CMD / DI   | MOSI   |      |
| 3.3V OUT  | VCC        | 电源   | **请勿**使用 5V |
| GND       | GND        | 地     |      |

如需更改引脚分配，在 `sdcard_port_pico.h` 中定义或通过编译器参数传入：

```c
#define SD_PORT_PICO_SPI      spi0
#define SD_PORT_PICO_PIN_MISO 16
#define SD_PORT_PICO_PIN_CS   17
#define SD_PORT_PICO_PIN_SCK  18
#define SD_PORT_PICO_PIN_MOSI 19
```

## 快速开始

### 1. 克隆并拉取子模块

```bash
git clone --recurse-submodules https://github.com/IotaHydrae/pico-littlefs-lib.git
cd pico-littlefs-lib
```

### 2. 构建

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

需要设置 `PICO_SDK_PATH` 环境变量指向有效的 Pico SDK 路径。

### 3. 烧录

```bash
sudo picotool load -fx ./example/pico-littlefs-demo.uf2
```

### 4. 查看串口输出

```bash
minicom -D /dev/ttyACM0
# 或者
cat /dev/ttyACM0
```

## 演示输出

插入 SDHC 卡后，串口输出如下：

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

## 架构

本库严格分离 **文件系统逻辑**、**块设备 I/O** 和 **硬件访问** 三层：

```
你的应用
  │
  ├── lfs.h                     ← littlefs 核心 API
  │     (format / mount / file I/O / directory)
  │
  ├── lfs_bd_sdcard.h           ← 块设备后端
  │     实现 lfs_config {read, prog, erase, sync}
  │     将 littlefs 块映射到 SD 卡扇区
  │
  └── sdcard.h / sdcard_port.h  ← SPI SD 卡协议 + 端口抽象
        └── sdcard_port_pico.c  ← Pico GPIO / SPI 实现
```

`lfs_bd_sdcard.c` 是一个**具体的后端实现**。`lfs_bd.h` 声明了通用抽象接口——每个后端负责填充自己的 `struct lfs_config`，包括几何参数和回调函数。

## 配置

默认几何参数（针对 SD 卡优化）：

| 字段 | 值 | 说明 |
|------|-----|------|
| `read_size` | 512 | SD 扇区大小 |
| `prog_size` | 512 | SD 扇区大小 |
| `block_size` | 4096 | 每个 littlefs 块 = 8 个扇区 |
| `cache_size` | 4096 | 单块缓存 |
| `lookahead_size` | 128 B | 可追踪 1024 个块 |
| `block_cycles` | 500 | 磨损均衡阈值 |

调用 `lfs_bd_sdcard_init()` 后，你可以在 `struct lfs_config` 上覆写这些字段，然后再调用 `lfs_format()` 或 `lfs_mount()`。

### 静态缓冲区

示例程序提供自己的读/编程/预读缓冲区，避免堆内存分配：

```c
static uint8_t read_buffer[4096];
static uint8_t prog_buffer[4096];
static uint32_t lookahead_buffer[128 / sizeof(uint32_t)];

lfs_bd_sdcard_init(&cfg, &bd);
cfg.read_buffer      = read_buffer;
cfg.prog_buffer      = prog_buffer;
cfg.lookahead_buffer = lookahead_buffer;
```

## API 使用

最小示例——格式化并挂载文件系统：

```c
#include "lfs.h"
#include "lfs_bd_sdcard.h"
#include "sdcard.h"

static lfs_bd_sdcard_t bd;
static struct lfs_config cfg;
static lfs_t lfs;

int main(void) {
    stdio_init_all();

    // 1. 初始化硬件
    sd_init();

    // 2. 将 littlefs 连接到 SD 卡
    lfs_bd_sdcard_init(&cfg, &bd);

    // 3. 格式化（首次使用）或挂载
    int err = lfs_mount(&lfs, &cfg);
    if (err == LFS_ERR_CORRUPT) {
        lfs_format(&lfs, &cfg);
        lfs_mount(&lfs, &cfg);
    }

    // 4. 使用文件系统
    lfs_file_t file;
    lfs_file_open(&lfs, &file, "data.bin",
                  LFS_O_WRONLY | LFS_O_CREAT);
    lfs_file_write(&lfs, &file, "hello", 5);
    lfs_file_close(&lfs, &file);

    lfs_unmount(&lfs);
}
```

## 添加新后端

要支持不同的存储设备（如 SPI NOR Flash），创建新文件：

```
lfs_bd_spi_flash.h
lfs_bd_spi_flash.c
```

实现以下回调（签名来自 `struct lfs_config`）：

| 回调 | 作用 |
|------|------|
| `read`  | 从 `block` 的 `off` 偏移处读取 `size` 字节到 `buffer` |
| `prog`  | 从 `buffer` 写 `size` 字节到 `block` 的 `off` 偏移处 |
| `erase` | 擦除 `block`（对不需要预擦除的介质可设为空操作） |
| `sync`  | 刷新缓存（对同步写入的介质可设为空操作） |

提供一个初始化函数，填充 `struct lfs_config` 的几何参数和上述回调。无需修改 `lfs_bd_sdcard.c`、`CMakeLists.txt` 或示例代码——只需添加新的源文件并链接即可。

## 内存占用

```
   text      data       bss       dec
  78448         0     12224     90672   ( ≈ 88.5 KiB )
```

- **Flash**: ~77 KiB（littlefs 核心 + SD 库 + 演示程序 + Pico SDK）
- **RAM**:  ~12 KiB（文件系统状态 + 缓存缓冲区 + SDK）

在 RP2040 的 2 MiB / 264 KiB 预算内完全充裕。

## 依赖

| 组件 | 来源 |
|------|------|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | `$PICO_SDK_PATH` |
| [littlefs](https://github.com/littlefs-project/littlefs) | git submodule `lib/littlefs` |
| [pico-sdcard-lib](https://github.com/IotaHydrae/pico-sdcard-lib) | git submodule `lib/sdcard-lib` |

## 许可证

本项目采用 MIT 许可证 — 详见 [LICENSE](LICENSE)。
版权 (c) 2025 Wooden Chair &lt;hua.zheng@embeddedboys.com&gt;。

LittleFS 版权归 (c) 2022 The LittleFS Authors 所有（BSD 3-Clause）。
SD 卡库版权归 (c) 2025 Wooden Chair 所有（MIT）。
