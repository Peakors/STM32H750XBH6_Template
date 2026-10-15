# STM32H750XBH6 Bootloader + APP 工程模板

基于反客 STM32H750XBH6 核心板（FK750M5-XBH6）的 **CLion + CubeMX + CMake + OpenOCD** 外部 Flash 下载算法模板，包含一个可引导 APP 从外部 QSPI Flash 就地执行（XIP）的 Bootloader。

**若使用当前模板的外部 Flash 下载算法，需要与原理图中 QSPI 引脚完全相同，否则无法使用。**

联系方式：Peakors@163.com

## 目录结构

```
STM32H750XBH6_Bootloader_APP_Template/
├── 1.Docs/                          # 项目文档（Markdown）
│   └── Pictures/                    # 文档配图
├── 2.Software/
│   ├── STM32H750XBH6_Bootloader/    # Bootloader：内部 Flash 引导程序
│   └── STM32H750XBH6_APP/           # APP：用户程序模板（USART1 + USB 虚拟串口）
├── 3.Hardware/                      # 核心板原理图、W25Q64 数据手册
├── 4.Release/                       # 预编译固件 hex + OpenOCD 烧写算法（开箱即用）
├── .github/workflows/               # CI：双工程自动构建
└── AGENTS.md                        # 协作规范（提交 / 测试 / 文档）
```

| 工程 | 运行位置 | 链接基址 | 说明 |
|---|---|---|---|
| `STM32H750XBH6_Bootloader` | 内部 Flash | `0x08000000`（128KB） | 上电初始化时钟与 QSPI 内存映射，跳转到 APP，约 30KB |
| `STM32H750XBH6_APP` | 外部 QSPI Flash（XIP） | `0x90000000`（8MB W25Q64） | printf 重定向到 USART1 + USB CDC 虚拟串口 |

## 快速开始

### 环境要求

- **STM32CubeCLT**（提供 arm-none-eabi-gcc、CMake、Ninja，并加入 PATH）——版本见下方「工程版本」
- **STM32CubeMX**（修改外设配置、重新生成代码时需要）
- **OpenOCD** + ST-Link 调试器（烧写/调试）
- 串口终端（115200-8-N-1，接 PA9/PA10）

环境搭建详见 [配置CLion用于STM32开发（OpenOCD下载）](1.Docs/配置CLion用于STM32开发（OpenOCD下载）.md)。

### 命令行构建

```bash
# 以 APP 工程为例（Bootloader 同理，替换目录名即可）
cd 2.Software/STM32H750XBH6_APP
cmake --preset Debug        # 或 Release
cmake --build --preset Debug

# 产物位于 build/Debug/：*.elf / *.bin / *.hex（两个工程均生成）
```

### 烧写与运行

**方式一：直接使用预编译固件（无需编译）**

`4.Release/` 提供了 Release 构建的固件 hex 与 OpenOCD 烧写算法（需已安装 OpenOCD 并连接 ST-Link）：

```bash
# 1. 烧写 Bootloader 到内部 Flash（0x08000000）
openocd -f 4.Release/OpenOCD/FK750M5-XBH6_Internal_Flash_Download_Algorithm.cfg \
        -c "program 4.Release/Bootloader/STM32H750XBH6_Bootloader.hex verify reset exit"

# 2. 烧写 APP 到外部 QSPI Flash（0x90000000，外部 Flash 下载算法）
openocd -f 4.Release/OpenOCD/FK750M5-XBH6_External_Flash_Download_Algorithm.cfg \
        -c "program 4.Release/APP/STM32H750XBH6_APP.hex verify reset exit"
```

**方式二：CLion 中一键下载** —— 两个工程分别配置 OpenOCD Download & Run，Board config file 选各自 `config/` 目录下的内部 / 外部 Flash 下载算法。

**复位运行**：Bootloader 打印初始化信息后跳转 APP，串口 1（115200-8-N-1）与 USB 虚拟串口均输出打印。

完整流程（含 CubeMX 从零配置、MPU/时钟/QSPI 参数核对、烧写验证）详见 [STM32H750XBH6_Bootloader_Guide](1.Docs/STM32H750XBH6_Bootloader_Guide.md)。

## 实现功能

- printf / scanf 重定向到串口 1，代码参考 `Library/retarget.c` 与 `retarget.h`，详见 [串口printf重定向移植与使用](1.Docs/串口printf重定向移植与使用.md)
- USB 虚拟串口（CDC），感谢群友 `琴梨project`，详见 [USB虚拟串口移植与使用](1.Docs/USB虚拟串口移植与使用.md)
- QSPI Bootloader 引导程序，详见 [STM32H750XBH6_Bootloader_Guide](1.Docs/STM32H750XBH6_Bootloader_Guide.md)

## 工程版本

- Firmware Package Name and Version: STM32Cube FW_H7 V1.13.0
- STM32CubeMX Version: 6.18.1
- STM32CubeCLT Version：1.22.0
- OpenOCD Version：20250710
- CLion 2026.2.3.1

## 文档索引

| 文档 | 内容 |
|---|---|
| [STM32H750XBH6_Bootloader_Guide.md](1.Docs/STM32H750XBH6_Bootloader_Guide.md) | Bootloader 从零复刻指南（原理、CubeMX 配置、代码实现、烧写验证） |
| [配置CLion用于STM32开发（OpenOCD下载）.md](1.Docs/配置CLion用于STM32开发（OpenOCD下载）.md) | CLion + OpenOCD 开发环境搭建 |
| [STM32Cubemx配置虚拟串口.md](1.Docs/STM32Cubemx配置虚拟串口.md) | CubeMX 中配置 USB 虚拟串口 |
| [USB虚拟串口移植与使用.md](1.Docs/USB虚拟串口移植与使用.md) | USB CDC 虚拟串口移植与使用 |
| [串口printf重定向移植与使用.md](1.Docs/串口printf重定向移植与使用.md) | printf/scanf 重定向到 USART1 |

## 硬件资料

- 原理图与 W25Q64 数据手册见 [3.Hardware](3.Hardware) 目录

## 感谢

- https://haobogu.github.io/posts/keyboard/openocd-ospi-flash
- https://github.com/haoruanwn/STM32H750XBH6_CMake_Template
- https://zhuanlan.zhihu.com/p/145801160
- https://github.com/WangHunZi/EmbeddedProjectTemplates

**pyOCD** 版本：https://github.com/haoruanwn/STM32H750XBH6_CMake_Template

开发板购买店铺：https://shop212360197.taobao.com

本项目测试板子：https://item.taobao.com/item.htm?id=682521953131

## 许可证

- 本项目自有代码采用 **GPL-2.0-or-later**（GNU General Public License v2.0 或任何更新的版本）发布，协议全文见 [LICENSE](LICENSE)。
- Copyright (C) 2025 Peakors
- 第三方代码保留其原始许可，不因本项目而改变：
  - `2.Software/*/Drivers/CMSIS`（Arm CMSIS）：Apache-2.0
  - `2.Software/*/Drivers/STM32H7xx_HAL_Driver`、`2.Software/*/Middlewares/ST/STM32_USB_Device_Library`（STMicroelectronics）：随 STM32 器件提供的原厂许可，按 AS-IS 提供
