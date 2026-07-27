# pico-littlefs

[English](README.md)

基于 [Pico SDK](https://github.com/raspberrypi/pico-sdk) 的 Raspberry Pi Pico (RP2040) LittleFS 移植。

支持 **SPI SD 卡** 和 **SPI NOR Flash**，通过块设备抽象层可无缝切换后端。

## 特性

- **LittleFS v2.x** — 掉电保护、磨损均衡的文件系统
- **SPI SD 卡** 后端，基于 [`pico-sdcard-lib`](https://github.com/IotaHydrae/pico-sdcard-lib)
- **SPI NOR Flash** 后端，基于 [`pico-spi-flash-lib`](https://github.com/IotaHydrae/pico-spi-flash-lib)
- **可插拔后端** — 无需修改现有代码即可添加新存储介质
- **零堆内存分配** — 所有缓冲区静态分配
- **自动几何检测** — PC 格式化的卡可直接挂载

## 项目结构

```
pico-littlefs-lib/
├── CMakeLists.txt                  # 顶层构建
├── lfs_bd.h                        # 块设备抽象接口
├── lfs_bd_sdcard.h / .c            # SD 卡后端
├── lfs_bd_flash.h / .c             # SPI NOR Flash 后端
├── sdcard_port_pico.h / .c         # SD 卡硬件端口
├── flash_port_pico.h               # Flash 引脚配置
├── pico_littlefs.h / .c            # 可选的便利封装
├── examples/
│   ├── sdcard_demo.c               # SD 卡演示
│   ├── sdcard_large_read.c         # SD 卡大文件读取基准测试
│   ├── flash_demo.c                # SPI Flash 演示
│   └── CMakeLists.txt
├── tests/                         # 稳定性测试
└── lib/
    ├── littlefs/                   # littlefs 上游源码
    ├── sdcard-lib/                 # SD 卡库
    └── spi-flash-lib/              # SPI Flash 库
```

## 快速开始

### 1. 克隆

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

需设置 `PICO_SDK_PATH` 指向有效的 Pico SDK 路径。

### 3. 烧录

```bash
# SD 卡演示
sudo picotool load -fx ./examples/pico-littlefs-sdcard-demo.uf2

# SD 卡大文件读取基准测试
sudo picotool load -fx ./examples/pico-littlefs-sdcard-large-read.uf2

# SPI Flash 演示
sudo picotool load -fx ./examples/pico-littlefs-flash-demo.uf2
```

### 4. 查看串口

```bash
minicom -D /dev/ttyACM0
```

## 接线

### SD 卡 (SPI0: GP16–19)

| Pico 引脚 | SD 卡引脚 | 信号 |
|-----------|-----------|------|
| GP16      | D0 / DO   | MISO |
| GP17      | D3 / CS   | CS   |
| GP18      | CLK / SCLK | SCK  |
| GP19      | CMD / DI  | MOSI |
| 3.3V OUT  | VCC       | 电源 |
| GND       | GND       | 地   |

### SPI Flash (SPI0: GP16–19，可与 SD 卡共用总线，不同 CS 即可)

| Pico 引脚 | Flash 引脚 | 信号 |
|-----------|------------|------|
| GP16      | DI / IO1   | MISO |
| GP17      | /CS        | CS   |
| GP18      | CLK        | SCK  |
| GP19      | DO / IO0   | MOSI |
| 3.3V OUT  | VCC        | 电源 |
| GND       | GND        | 地   |

## 架构

```
你的应用
  │
  ├── lfs.h                          ← littlefs 核心 API
  │
  ├── lfs_bd_sdcard.c                ← SD 卡后端
  │     read/prog/erase/sync，块→扇区映射
  │
  ├── lfs_bd_flash.c                 ← Flash 后端
  │     read/prog/erase/sync，块→字节地址映射
  │
  └── sdcard.h / flash.h             ← 硬件驱动库
        └── *_port_pico.c            ← Pico SPI / GPIO
```

## 后端对比

| | SD 卡 | SPI NOR Flash |
|---|---|---|
| **默认 block_size** | 16384 (32 扇区) | 4096 |
| **read/prog 对齐** | 512 字节 | 1 字节 |
| **Erase** | no-op（卡自管理） | `spi_flash_erase_sector()` |
| **读取速度** | ~600 KiB/s (SPI) | ~1 MiB/s (SPI) |
| **端口文件** | `sdcard_port_pico.c` | `flash_port_pico.c` |

## 移植新存储器件

添加新后端（如 NAND Flash、eMMC、PSRAM）只需创建两个文件，无需修改现有代码：

### 1. 创建后端头文件 — `lfs_bd_<name>.h`

```c
#ifndef LFS_BD_MYDEVICE_H
#define LFS_BD_MYDEVICE_H

#include "lfs.h"
#include <stdint.h>

#ifndef LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE
#define LFS_BD_MYDEVICE_DEFAULT_BLOCK_SIZE 4096
#endif

typedef struct lfs_bd_mydevice {
    uint32_t size_limit;    /* 0 = 自动检测 */
    uint32_t byte_offset;   /* 通常为 0 */
} lfs_bd_mydevice_t;

int lfs_bd_mydevice_init(struct lfs_config *cfg, lfs_bd_mydevice_t *bd);

#endif
```

### 2. 实现四个回调 — `lfs_bd_<name>.c`

```c
#include "lfs_bd_mydevice.h"
#include "mydevice.h"   /* 你的硬件驱动 */

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
    lfs_bd_mydevice_t *bd = (lfs_bd_mydevice_t *)c->context;
    uint32_t addr = bd->byte_offset + block * c->block_size + off;
    return mydevice_write(addr, buffer, size) == OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int bd_mydevice_erase(const struct lfs_config *c, lfs_block_t block)
{
    /* Flash：调用硬件擦除。SD/eMMC：空操作即可。 */
    return LFS_ERR_OK;
}

static int bd_mydevice_sync(const struct lfs_config *c)
{
    /* 如果写入有缓存则刷新，否则空操作。 */
    return LFS_ERR_OK;
}

int lfs_bd_mydevice_init(struct lfs_config *cfg, lfs_bd_mydevice_t *bd)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->read_size  = 1;         /* 按字节或扇区对齐 */
    cfg->prog_size  = 1;         /* 同上 */
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

### 3. 接入构建系统

在顶层 `CMakeLists.txt` 的 `pico_littlefs_bd` 中添加源文件：

```cmake
target_sources(pico_littlefs_bd PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lfs_bd_mydevice.c)
```

### 各存储介质关键参数

| | NOR Flash | SD/eMMC | NAND Flash |
|---|---|---|---|
| **read_size** | 1（字节） | 512（扇区） | page 大小 |
| **prog_size** | 1（字节） | 512（扇区） | page 大小 |
| **erase** | 真实擦除 | 空操作 | 真实擦除 |
| **sync** | 空操作 | 空操作 | 刷新缓存 |
| **block_size** | 4096 | 16384 | 擦除块大小 |
| **block_count** | 容量 ÷ block_size | 扇区数 ÷ (bs/512) | 容量 ÷ block_size |

### 参考实现

- `lfs_bd_sdcard.c` — 512 字节扇区 I/O、CMD18 多块读、空擦除
- `lfs_bd_flash.c` — 字节级 I/O、真实擦除、空同步

## 测试

`tests/` 目录包含硬件级稳定性测试。

### Flash 压力测试

在 SPI NOR Flash 上反复格式化、写入、读取、校验和删除随机大小的文件，设计用于长时间无人值守运行以捕获间歇性问题。

```bash
# 默认：24 小时，每 30 分钟报告
sudo picotool load -fx ./tests/pico-flash-stress.uf2

# 快速冒烟：1 小时，每 5 分钟报告
cmake -G Ninja -DSTRESS_DURATION_HOURS=1 -DSTRESS_STATUS_INTERVAL_MIN=5 ..
ninja

# 无限运行，每 10 分钟报告
cmake -G Ninja -DSTRESS_DURATION_HOURS=0 -DSTRESS_STATUS_INTERVAL_MIN=10 ..
ninja
```

操作混合（加权随机）：
- 40 % 读取 + 逐字节校验（确定性 per-file pattern）
- 25 % 追加写入（128 B – 2 KiB 随机额外数据）
- 15 % 删除（Flash 使用率超过 70% 时触发）
- 10 % 创建（256 B – 128 KiB 随机大小，最多 128 个文件）
- 10 % 列出根目录

状态同时输出到串口和 Flash 文件系统中的 `/var/stress_test_<编译日期>.log`。每次写入后立即同步，断电不丢日志。

## PC 跨平台挂载（SD 卡）

```
Pico 格式化 → PC:  ./lfs --block_size=16384 /dev/sda mount
PC 格式化   → Pico: lfs_bd_sdcard_mount_auto() 自动检测
```

## 依赖

| 组件 | 来源 |
|------|------|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | `$PICO_SDK_PATH` |
| [littlefs](https://github.com/littlefs-project/littlefs) | `lib/littlefs` |
| [pico-sdcard-lib](https://github.com/IotaHydrae/pico-sdcard-lib) | `lib/sdcard-lib` |
| [pico-spi-flash-lib](https://github.com/IotaHydrae/pico-spi-flash-lib) | `lib/spi-flash-lib` |

## 许可证

MIT — 详见 [LICENSE](LICENSE)。版权所有 (c) 2025 Wooden Chair &lt;hua.zheng@embeddedboys.com&gt;。

LittleFS 版权所有 (c) 2022 The LittleFS Authors（BSD 3-Clause）。
SD 卡库版权所有 (c) 2025 Wooden Chair（MIT）。
SPI Flash 库版权所有 (c) 2025 Wooden Chair（MIT）。
