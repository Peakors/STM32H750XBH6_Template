# STM32H750XBH6 QSPI Bootloader 从零复刻指南

> 一个基于 STM32H750XBH6 + W25Q64 的 QSPI Bootloader 工程 —— 从环境搭建、外设配置、代码实现到烧写验证的完整复刻文档。

| 项目 | 内容 |
|---|---|
| 文档版本 | v1.1 |
| 目标读者 | 完全不了解 bootloader 的嵌入式初学者；会基本 C 语言、用过 CubeMX 即可 |
| 文档目标 | 照本文档操作，可以从零复刻出整个可工作的引导程序 |
| 硬件平台 | STM32H750XBH6（TFBGA240）+ W25Q64 QSPI Flash 核心板 |
| 工程名 | `STM32H750XBH6_Bootloader` |
| 驱动来源 | `BSP/Src/qspi_w25q64.c` 中的 W25Qxx 命令协议部分参考自开发板公开的 W25Q64 驱动改写，其余全部代码为本工程原创 |
| 验证状态 | 已在真实硬件上烧写并运行通过 |

---

## 阅读约定

**符号约定**

| 标记 | 含义 |
|---|---|
| ⚠️ **注意** | 容易忽略、需要特别留意的操作要点 |
| ✅ **已验证** | 在真实硬件或真实工具链上验证过的事实 |
| 💡 **原理** | 解释"为什么是这个值"，可跳过但建议看 |
| 📷 **【截图占位】** | 需要你自己补一张界面截图的位置 |
| 🚧 **未验证/思路** | 未在本工程中实际实现，仅给出方向 |

**路径约定**

- 工程根目录记作 `<工程根>`，即 `STM32H750XBH6_Bootloader/`
- Windows 路径统一用反斜杠；在 CubeMX / CMake 界面里填路径时正反斜杠都可以

**代码呈现约定**

正文只贴**关键片段**并标注文件名与行号，完整源码请看工程本身。

> ⚠️ **关于行号**：本文档标注的行号以写作时的工程状态为准，且**始终同时给出该行的代码内容**，请以"搜索代码内容"为主要定位手段。行号会因以下原因漂移：
> 1. 你在 `/* USER CODE */` 段里增删了代码；
> 2. 你用 CubeMX 重新生成了代码；
> 3. **你用 IDE 执行了"重新格式化代码"** —— 本工程在 CLion 里被格式化过一次，所以 `Core/Src/main.c` 等文件现在是"大括号同行 + 4 空格缩进"，而 CubeMX 原始生成的是"大括号另起一行 + 2 空格缩进"。两者功能完全等价。

**内部链接约定**

文档内的章节跳转链接（形如 `[3.7](#37-...)`）在 GitHub / VS Code / Typora 等标准 Markdown 渲染器里可用。

> ⚠️ 部分标题里含有 emoji（★ / ⚠️）和特殊符号，不同渲染器生成的锚点可能不一致。**如果某个链接点不动，请直接按章节号（如"3.7"）在文档里搜索。**

---

## 目录

- [第 0 章 文档说明](#第-0-章-文档说明)
- [第 1 章 背景与原理](#第-1-章-背景与原理)
- [第 2 章 环境准备](#第-2-章-环境准备)
- [第 3 章 用 CubeMX 从零建工程](#第-3-章-用-cubemx-从零建工程)
- [第 4 章 生成代码核对清单](#第-4-章-生成代码核对清单)
- [第 5 章 BSP 层：W25Q64 驱动](#第-5-章-bsp-层w25q64-驱动)
- [第 6 章 Library 层：printf 重定向](#第-6-章-library-层printf-重定向)
- [第 7 章 引导流程实现](#第-7-章-引导流程实现)
- [第 8 章 CMake 构建系统](#第-8-章-cmake-构建系统)
- [第 9 章 烧写、运行与验证](#第-9-章-烧写运行与验证)
- [第 10 章 关键设计决策](#第-10-章-关键设计决策)
- [第 11 章 移植与扩展](#第-11-章-移植与扩展)
- [附录 A 文件清单与关键行号](#附录-a-文件清单与关键行号)
- [附录 B 关键地址与寄存器速查](#附录-b-关键地址与寄存器速查)
- [附录 C ARMv7-M 默认内存映射表](#附录-c-armv7-m-默认内存映射表)
- [附录 D .ioc 文件全文](#附录-d-ioc-文件全文)
- [附录 E 版本记录](#附录-e-版本记录)
- [附录 F 参考资料](#附录-f-参考资料)

---

# 第 0 章 文档说明

## 0.1 这份文档解决什么问题

STM32H750 这颗芯片有一个"先天缺陷"：它标称 128KB Flash，而这个容量对于稍微复杂一点的程序都不够用。ST 官方的解法是——**把用户程序放到外部的 QSPI Flash 里，让 CPU 直接从 QSPI Flash 取指令执行**（称为 XIP，eXecute In Place）。

但这带来一个新问题：CPU 上电复位后只会从内部 Flash 的 `0x08000000` 开始执行，它并不知道外部 QSPI Flash 里有什么。所以需要一个"先遣队"程序：

1. 配好时钟（QSPI 需要很高的时钟才能跑得快）
2. 初始化 QSPI 外设
3. 把 QSPI Flash 配置成"内存映射模式"——配置完之后，QSPI Flash 里的内容就**像普通内存一样**出现在地址 `0x90000000`
4. 从 `0x90000000` 读出用户程序的栈顶地址和入口地址
5. 把 CPU 交出去（跳转）

这个"先遣队"就是 **bootloader**（本文简称 **boot**）。它自己必须能塞进 128KB 内部 Flash 里，所以要做得很小——本工程的 boot 编译出来只有 **约 30KB**。

```
┌─────────────────────────────────────────────────────────────┐
│  内部 Flash (128KB)          外部 QSPI Flash (W25Q64, 8MB)  │
│  0x08000000                  0x90000000                     │
│  ┌───────────────┐           ┌───────────────────────┐      │
│  │  Bootloader   │  ──跳转──▶ │   用户程序 (app)      │      │
│  │  ~30KB        │           │   向量表在最前面      │      │
│  └───────────────┘           └───────────────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

## 0.2 阅读顺序建议

| 你的情况 | 建议 |
|---|---|
| 第一次接触 bootloader | 从第 1 章顺序读到底 |
| 只想赶紧把工程跑起来 | 第 2 章 → 第 3 章 → 第 8 章 → 第 9 章 |
| 只想理解代码 | 第 1 章 → 第 5 章 → 第 7 章 |
| 已经跑通了，想改硬件 | 第 11 章「移植与扩展」 |
| 写代码或改配置时对照自查 | 第 10 章「关键设计决策」 |

## 0.3 配套代码位置

本文档描述的工程就是 `<工程根>` 本身。关键目录：

```
<工程根>/
├── Core/          ← CubeMX 生成的外设初始化代码（不要手改，会被覆盖）
├── BSP/           ← 板级支持包：W25Q64 驱动 + LED 宏（手写）
├── Library/       ← printf 重定向库（手写）
├── Drivers/       ← ST 官方 HAL 库 + CMSIS（CubeMX 拷贝进来，不要改）
├── cmake/         ← CubeMX 生成的构建脚本
└── build/         ← 构建输出目录（.gitignore 已忽略，不入库）
```

> 💡 本文档及仓库其余文档统一放在**仓库根目录的 `1.Docs/`** 下，不在 `<工程根>` 内。

完整的带说明文件树见 [附录 A](#附录-a-文件清单与关键行号)。

## 0.4 版本信息

本文档涉及的全部软件版本（详见 [附录 E](#附录-e-版本记录)）：

| 软件 | 版本 |
|---|---|
| STM32CubeMX | 6.18.1 |
| STM32Cube 固件包 FW_H7 | V1.13.0 |
| STM32CubeCLT（工具链集合） | 1.22.0 |
| arm-none-eabi-gcc | 14.3.1（GNU Tools for STM32 14.3.rel1.20251027-0700） |
| CMake | 4.3.1（随 CubeCLT 安装） |
| CLion | 2026.2.3.1（可选，用命令行也能构建） |

---

# 第 1 章 背景与原理

## 1.1 什么是 bootloader

**一句话**：bootloader 是上电后第一个运行的程序，它的唯一任务是把真正要跑的程序"请出来"并交班。

它和普通程序的区别：

| | 普通程序 | bootloader |
|---|---|---|
| 运行位置 | 内部 Flash（或外部） | **必须在内部 Flash**（CPU 复位后只能从那执行） |
| 体积 | 随意 | 必须很小（本芯片只有 128KB） |
| 生命周期 | 一直运行 | **运行完后"消失"**（把 CPU 交给另一个程序） |
| 外设依赖 | 按需 | 只初始化跳转所必需的外设 |

"交班"这个动作在 C 语言层面就是一次**函数指针调用**，但要做得正确，需要满足硬件上的几个前提（第 7 章详述）。

## 1.2 为什么 STM32H750 一定要 bootloader

STM32H750 属于 STM32H7 系列，它的 Flash 配置比较特殊：

| 型号 | 内部 Flash | 说明 |
|---|---|---|
| STM32H743 / H753 | 1MB ~ 2MB | 程序直接放内部 Flash 就行，不需要 boot |
| **STM32H750** | **128KB** | **不够用，必须外挂存储器** |

H750 的 128KB 内部 Flash 放不下带 GUI、网络协议栈、文件系统的应用。ST 因此提供了 XIP 方案：程序烧到外部 QSPI Flash，CPU 直接从 QSPI 读指令执行。

💡 **原理：内存映射模式（Memory-Mapped Mode）**

QUADSPI 外设有两种工作方式：

1. **间接模式**：CPU 写寄存器 → QSPI 外设发命令 → 读回数据到寄存器。每次访问都要 CPU 参与，很慢。
2. **内存映射模式**：把"读 Flash"这件事**硬件化**。配置好之后，CPU 只要访问地址 `0x90000000 + offset`，QUADSPI 硬件就自动发出读命令、把数据返回给 CPU。**CPU 完全不需要知道底下是 Flash**。

进入内存映射模式后这块 Flash 就"伪装"成了一段内存。注意：**内存映射模式下只能读、不能写**。要写 Flash 必须先退出内存映射模式，用间接模式发擦除/编程命令（第 5 章的驱动就是这么做的）。

## 1.3 地址布局

### 1.3.1 本工程用到的地址

| 地址范围 | 大小 | 属于什么 | 本工程的用途 |
|---|---|---|---|
| `0x0000_0000` ~ `0x0000_FFFF` | 64KB | ITCM RAM（可重映射为 Flash 别名） | 未使用 |
| `0x0800_0000` ~ `0x0801_FFFF` | **128KB** | **内部 Flash** | **bootloader 自己** |
| `0x2000_0000` ~ `0x2001_FFFF` | 128KB | DTCM RAM | boot 的栈、堆、全局变量 |
| `0x2400_0000` ~ `0x2407_FFFF` | 512KB | AXI SRAM（D1 域） | 未使用（留给 app） |
| `0x3000_0000` ~ `0x3004_7FFF` | 288KB | AHB SRAM（D2 域） | 未使用（留给 app） |
| `0x3800_0000` ~ `0x3800_FFFF` | 64KB | AHB SRAM（D3 域） | 未使用（留给 app） |
| **`0x9000_0000`** | **8MB** | **QSPI Flash 内存映射区** | **用户程序所在位置** |

✅ **已验证**：这些地址全部来自本工程的链接脚本 `<工程根>/STM32H750xx_FLASH.ld`：

```ld
/* <工程根>/STM32H750xx_FLASH.ld:58-63 */
DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
RAM (xrw)      : ORIGIN = 0x24000000, LENGTH = 512K
RAM_D2 (xrw)   : ORIGIN = 0x30000000, LENGTH = 288K
RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
FLASH (rx)     : ORIGIN = 0x8000000, LENGTH = 128K
```

> ⚠️ **注意**：`0x90000000` 是 QUADSPI **Bank 1** 的固定映射地址，由芯片硬件决定，不是随便选的。Bank 2 对应的地址是 `0x70000000`（本工程未使用）。

### 1.3.2 用户程序（app）自己的布局

app 烧在 QSPI Flash 的偏移 0 处，它看到的地址是 `0x90000000` 起。app 的链接脚本必须把 Flash 起始地址设成 `0x90000000`，并且**向量表必须放在最前面**：

```
0x90000000  ┌──────────────────────┐
            │ 初始 MSP（栈顶地址） │ ← boot 读这个值，写进 CPU 的 MSP 寄存器
0x90000004  ├──────────────────────┤
            │ Reset_Handler 地址   │ ← boot 读这个值，作为跳转目标
0x90000008  ├──────────────────────┤
            │ 其它中断向量 ...     │
            └──────────────────────┘
```

这不是我们规定的，而是 **ARM Cortex-M 的硬件约定**：CPU 复位时自动从向量表偏移 0 读 MSP、偏移 4 读 PC。boot 要做的事情就是手工模拟这个动作。

## 1.4 完整流程图

```
上电 / 复位
   │
   ▼
① 硬件从 0x08000000 取 MSP、从 0x08000004 取 PC  ← 纯硬件行为
   │
   ▼
② main() 开始
   │
   ├─ MPU_Config()         配置 MPU：区域0（背景保护区）+ 区域1（放行 QSPI 区）
   ├─ SCB_EnableICache()   开指令缓存
   ├─ SCB_EnableDCache()   开数据缓存
   ├─ HAL_Init()           初始化 HAL、Systick
   ├─ SystemClock_Config() 配时钟：HSE 25M → PLL → 480MHz
   │
   ├─ MX_GPIO_Init()       配 PC13（LED）
   ├─ MX_QUADSPI_Init()    配 QUADSPI 外设 + 内核时钟 + 6 根引脚
   ├─ MX_USART1_UART_Init()配串口 115200
   │
   ├─ RetargetInit(&huart1)  把 printf 接到串口
   ├─ printf("***")          打印分隔线
   │
   ├─ QSPI_W25Qxx_Init()     复位 W25Q64、读 JEDEC ID、校验
   │      └─ 失败 → 打印错误，停在 while(1)
   │
   ├─ QSPI_W25Qxx_MemoryMappedMode()  进入内存映射模式
   │      └─ 此后 0x90000000 可以被读
   │
   ├─ 从 0x90000004 读入口地址 → 存全局变量 JumpToApplication
   ├─ 从 0x90000000 读初始 MSP → 存全局变量 AppStackPointer
   │
   ├─ SCB_DisableICache()    关指令缓存
   ├─ SCB_DisableDCache()    关数据缓存（内部会先 clean+invalidate）
   ├─ SysTick->CTRL=0 ...    停 Systick
   ├─ HAL_MPU_Disable()      关 MPU，恢复到复位态
   │
   ├─ __set_MSP(AppStackPointer)  把 CPU 的栈指针换成 app 的
   └─ JumpToApplication()        跳过去（此后 boot 不再运行）
```

## 1.5 硬件平台与接线

### 1.5.1 平台

| 项目 | 型号/参数 |
|---|---|
| 核心板 | STM32H750XBH6 核心板 + W25Q64 |
| 主控 | STM32H750XBH6，TFBGA240 封装，Cortex-M7 @480MHz |
| 外部 Flash | W25Q64JV，8MB，QSPI 接口，JEDEC ID = `0xEF4017` |
| 晶振 | HSE 25MHz（板载） |
| 调试器 | ST-Link（SWD） |

📷 **【截图占位】** 核心板实物照片（顶视 + 底视），需体现 QSPI Flash 芯片位置、LED 位置、SWD 排针、串口引脚。
建议在图上用箭头标注：W25Q64、LED1(PC13)、USART1 TX/RX、SWDIO/SWCLK、HSE 晶振。

### 1.5.2 引脚分配

✅ **已验证**：以下引脚分配与本工程 CubeMX 里的配置完全一致。

| 功能 | 引脚 | 复用/模式 | 备注 |
|---|---|---|---|
| LED1 | **PC13** | GPIO 推挽输出 | 低电平点亮，CubeMX 里 Label 为 `LED1` |
| USART1 TX | **PA9** | AF7 | 115200-8-N-1，接 USB 转串口模块的 RX |
| USART1 RX | **PA10** | AF7 | 接 USB 转串口模块的 TX |
| QUADSPI_CLK | **PF10** | AF9 | |
| QUADSPI_BK1_NCS | **PG6** | AF10 | 片选 |
| QUADSPI_BK1_IO0 | **PF8** | AF10 | |
| QUADSPI_BK1_IO1 | **PF9** | AF10 | |
| QUADSPI_BK1_IO2 | **PF7** | AF9 | |
| QUADSPI_BK1_IO3 | **PF6** | AF9 | |
| SWDIO / SWCLK | PA13 / PA14 | 调试口 | 保留，不要禁用 |
| HSE | PH0 / PH1 | 外部晶振 | 25MHz |

引脚的复用号（AF）由芯片数据手册的引脚复用表决定，在 CubeMX 里选中外设功能后会自动填入正确的值。**核对时只需确认复用的是 AF7（USART1）/ AF9 / AF10（QUADSPI）**，具体对应关系见上表。

> 💡 对照 CubeMX 生成的 `Core/Src/quadspi.c` 可以验证这 6 根脚：一次 `HAL_GPIO_Init(GPIOG, ...)` 用的是 `GPIO_AF10_QUADSPI`（PG6 = NCS），两次 `HAL_GPIO_Init(GPIOF, ...)` 分别用 `GPIO_AF9_QUADSPI`（PF6/PF7/PF10）和 `GPIO_AF10_QUADSPI`（PF8/PF9）。

## 1.6 技术选型：为什么用 CubeMX + CMake + GCC

本工程采用 **STM32CubeMX（外设配置与代码生成）+ CMake（构建系统）+ arm-none-eabi-gcc（编译器）** 的组合。

### 1.6.1 选型理由

| 原因 | 说明 |
|---|---|
| 工具链免费且跨平台 | GCC 工具链完全免费，不受商业授权和代码大小限制 |
| 便于 CI / 自动化 | CMake 工程可以在命令行、CI 服务器上无界面构建 |
| 便于版本管理 | `.ioc`、`CMakeLists.txt`、`.ld` 全是纯文本，diff 友好、冲突易解 |
| 一键重新生成外设代码 | 改外设配置只需在 CubeMX 里改，初始化代码自动更新；用户代码受 `USER CODE` 段保护 |
| IDE 支持好 | CLion / VS Code 对 CMake 支持最好，补全、跳转、调试都可用 |

### 1.6.2 GCC 工具链的几个特点（写代码时需要注意）

✅ **已验证**：以下是本工程在 GCC 下实际遇到并已处理的点，每一处在第 5、6、10 章都有详细说明。

| 特点 | 说明 | 本工程怎么处理 |
|---|---|---|
| `printf` 走 `_write`，**不经过 `fputc`** | GCC 用 newlib，标准库的写请求最终调用 `_write()` | 专门做了 `Library/` 层重定向（[第 6 章](#第-6-章-library-层printf-重定向)）|
| `uint32_t` 的实际类型是 **`unsigned long`** | 与把 `uint32_t` 映射为 `unsigned int` 的工具链不同 | `printf("%X", ...)` 必须强转 `(unsigned int)`（[5.5](#55-w25qxx-驱动的来源与改动说明)）|
| 弱符号可被强符号覆盖 | `__attribute__((weak))` 定义的符号会被同名强定义取代 | 用它解决 `syscalls.c` 与重定向库的冲突（见 6.3）|
| 链接脚本是 `.ld`（GNU 语法）| 各工具链的链接脚本格式互不通用 | 用 CubeMX 生成的 `STM32H750xx_FLASH.ld` |
| 启动文件是 GNU 汇编语法 | `startup_stm32h750xx.s` 不能与 ARM 汇编语法的版本互换 | 用 CubeMX 为本工程生成的启动文件 |
| 用 `--specs=nano.specs`（newlib-nano）| 体积小，但默认不支持 `%f` 浮点格式输出 | boot 不打印浮点数，无影响 |

## 1.7 术语表

| 术语 | 全称 / 英文 | 含义 |
|---|---|---|
| **boot / bootloader** | Bootloader | 引导程序，上电后第一个运行、负责跳转到 app 的程序 |
| **app** | Application | 用户程序，本工程里指烧在 QSPI Flash 里的那个程序 |
| **XIP** | eXecute In Place | 就地执行，CPU 直接从外部存储器取指令执行，不搬到 RAM |
| **QSPI** | Quad SPI | 四线 SPI，比普通 SPI 快 4 倍；STM32H7 里的外设名是 QUADSPI |
| **内存映射模式** | Memory-Mapped Mode | QSPI 的一种工作方式，Flash 内容直接出现在 CPU 地址空间 |
| **间接模式** | Indirect Mode | QSPI 的另一种方式，通过寄存器收发数据，可读可写 |
| **JEDEC ID** | — | Flash 芯片的厂商+型号标识，W25Q64 是 `0xEF4017` |
| **W25Q64** | — | Winbond 的 8MB QSPI Flash 芯片 |
| **MSP** | Main Stack Pointer | 主栈指针，CPU 的栈顶寄存器；复位时从向量表[0] 加载 |
| **向量表** | Vector Table | 存放在 Flash 最前面的一张表：第 0 项是初始 MSP，第 1 项是复位入口… |
| **MPU** | Memory Protection Unit | 内存保护单元，给地址范围打上访问权限/缓存属性 |
| **MemManage Fault** | — | MPU 违规访问时触发的异常（若未使能则升级为 HardFault） |
| **Cache** | I-Cache / D-Cache | CPU 里的指令/数据缓存，Cortex-M7 才有 |
| **DTCM** | Data Tightly Coupled Memory | 紧耦合数据 RAM，速度最快，`0x20000000`，本工程栈所在处 |
| **HAL** | Hardware Abstraction Layer | ST 的硬件抽象层库 |
| **CubeMX** | STM32CubeMX | ST 的图形化配置工具，生成外设初始化代码 |
| **CubeCLT** | STM32CubeCLT | ST 的命令行工具链集合（编译器+CMake+Ninja+烧写工具） |
| **.ioc** | — | CubeMX 工程的配置存档文件（纯文本，可版本管理） |
| **AutoPolling** | 自动轮询 | QSPI 的一种硬件机制：自动反复读状态寄存器直到满足条件，无需 CPU 干预 |

---

# 第 2 章 环境准备

本章目标是：把一台干净的 Windows 电脑准备成可以编译本工程的状态。做完后你应该能在命令行里看到编译器版本号。

## 2.1 需要安装的软件

### 2.1.1 必装

| 软件 | 本工程验证过的版本 | 用途 | 下载位置 |
|---|---|---|---|
| **STM32CubeMX** | 6.18.1 | 图形化配置外设、生成初始化代码 | ST 官网搜索 "STM32CubeMX" |
| **STM32Cube FW_H7 固件包** | **V1.13.0** | HAL 库源码 + CMSIS（CubeMX 里在线安装） | CubeMX 内 `Help → Manage embedded software packages` |
| **STM32CubeCLT** | **1.22.0** | 工具链集合：arm-none-eabi-gcc 14.3.1、CMake 4.3.1、Ninja、GNU Make、ST-Link GDB Server、CubeProgrammer CLI | ST 官网搜索 "STM32CubeCLT" |
| **串口终端** | 任意 | 看 boot 打印的信息 | PuTTY / MobaXterm / VS Code 串口插件 / 任意 |

> ⚠️ **固件包版本很重要**。本工程用的是 **FW_H7 V1.13.0**。用更新的版本（如 V1.14+）通常也能编译，但 HAL API 偶有变化，如果遇到编译错误，请先回到 V1.13.0 排除版本因素。
> 判断方法：CubeMX 里 `Project Manager → Project → Firmware Package` 显示 `STM32Cube FW_H7 V1.13.0` 即为正确（见 [附录 D](#附录-d-ioc-文件全文) 第 123 行）。

### 2.1.2 选装

| 软件 | 版本 | 用途 |
|---|---|---|
| STM32CubeProgrammer（GUI） | 任意较新版 | 图形化烧写。CubeCLT 里已带命令行版，装 GUI 只是为了方便 |
| CLion | 2026.2.3.1 | 图形化 IDE，本工程实测可用。**用命令行也能完整构建** |
| VS Code + CMake Tools | 任意 | 轻量替代方案 |
| Git | 任意 | 版本管理（`.ioc`、CMake 脚本都是纯文本，很适合 Git） |

### 2.1.3 硬件

| 物品 | 说明 |
|---|---|
| STM32H750XBH6 核心板（带 W25Q64）| 引脚分配必须与 [1.5.2](#152-引脚分配) 一致 |
| ST-Link V2 / V2-1 / V3 | 烧写与调试 |
| USB 转串口模块 | 3.3V 电平，接 PA9/PA10，用来收 boot 的打印 |
| 杜邦线若干 | |

## 2.2 安装顺序与注意点

按以下顺序安装，**每装完一个就做一次 2.3 的自检**，问题早发现早解决：

1. **STM32CubeMX** → 安装时勾选 "Install STM32CubeCLT"（如果安装器提供该选项，可省掉第 3 步）
2. **固件包 FW_H7 V1.13.0** → 打开 CubeMX，`Help → Manage embedded software packages → STM32Cube MCU Package for STM32H7 Series` → 选 **V1.13.0** → Install
3. **STM32CubeCLT 1.22.0** → 默认安装路径为 `C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0`
4. **STM32CubeProgrammer** → 安装时勾选 "Add to PATH"
5. **（选装）CLion**

💡 **原理：CubeCLT 里都有什么**

| 子目录 | 内容 |
|---|---|
| `GNU-tools-for-STM32\bin` | **arm-none-eabi-gcc / g++ / objcopy / size / gdb** ← 编译器，最关键的目录 |
| `CMake\bin` | `cmake.exe` |
| `Ninja\bin` | `ninja.exe`（构建工具） |
| `Make\bin` | `make.exe`（GNU Make 的 Windows 版） |
| `STM32CubeProgrammer\bin` | `STM32_Programmer_CLI.exe`（命令行烧写） |
| `STLink-gdb-server\bin` | GDB Server（调试用） |
| `st-arm-clang\bin` | ST 定制的 Clang 工具链（本工程**不用**） |
| `STM32target-mcu` | 各芯片的 SVD/寄存器描述 |

本工程用的是 **GCC** 工具链（`cmake/gcc-arm-none-eabi.cmake`），不是 ST 的 Clang。

## 2.3 环境自检

打开**一个新的** `cmd` 或 PowerShell 窗口，依次执行：

```bat
where arm-none-eabi-gcc
where arm-none-eabi-g++
where cmake
where ninja
where make
```

✅ **已验证**：全部能找到时的正确结果应类似（路径里的版本号可能不同）：

```
C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe
C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-g++.exe
C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0\CMake\bin\cmake.exe
C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0\Ninja\bin\ninja.exe
C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0\Make\bin\make.exe
```

再确认版本号：

```bat
arm-none-eabi-gcc --version
cmake --version
```

✅ **已验证**：本工程验证过的输出：

```
arm-none-eabi-gcc.exe (GNU Tools for STM32 14.3.rel1.20251027-0700) 14.3.1 20250623
cmake version 4.3.1
```

> 💡 只要 `where arm-none-eabi-gcc` 能打印出路径，后面 CMake 就一定找得到编译器 —— 因为工程的 `cmake/gcc-arm-none-eabi.cmake:11` 写的是**裸名字**：
> ```cmake
> set(TOOLCHAIN_PREFIX arm-none-eabi-)
> set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)   # 即 "arm-none-eabi-gcc"，靠 PATH 解析
> ```
> 这是 STM32CubeMX 生成 CMake 工程的**标准行为**，也是本工程可移植性的基础：**换电脑只要能 `where` 到编译器就能构建**。

---

# 第 3 章 用 CubeMX 从零建工程

> 本章是全文最长的一章。其中 **3.7（MPU 区域 1）** 和 **3.8（QUADSPI）** 的配置项较多，建议不要跳读。
> 每一步都给了"界面在哪"和"值填什么"，并留了截图占位。

## 3.1 新建工程与芯片选型

| 步骤 | 操作 |
|---|---|
| 1 | 打开 STM32CubeMX → `File → New Project` |
| 2 | 在 `Part Number` 搜索框输入 **`STM32H750XBHx`** |
| 3 | 在左侧列表里选中 **STM32H750XBHx** |
| 4 | 右侧预览区确认 `Package` 是 **TFBGA240**（H750 只有这一种封装，应自动匹配） |
| 5 | 点右下角 **`Start Project`** |

📷 **【截图占位】** CubeMX 新建工程界面（MCU 选择页）。
需体现：搜索框里的 `STM32H750XBHx`、左侧选中项、右侧 `Package: TFBGA240`、右下角 `Start Project` 按钮。

✅ **已验证**：选完后 `.ioc` 里应有：

```
Mcu.CPN=STM32H750XBH6
Mcu.Name=STM32H750XBHx
Mcu.Package=TFBGA240
Mcu.Family=STM32H7
```

## 3.2 Project Manager 设置

在 `Project Manager` 标签页里逐项设置。

### 3.2.1 Project 子页

| 项目 | 值 | 说明 |
|---|---|---|
| Project Name | `STM32H750XBH6_Bootloader` | 这个名字会决定 CMake 的目标名和输出的 `.elf`/`.map` 文件名 |
| Project Location | 你的工程父目录 | 例如 `C:\Users\<你>\Desktop\STM32Code\stm32h7_download` |
| Application Structure | `Advanced`（默认） | 决定生成 `Core/Inc`、`Core/Src` 这种结构 |
| **Toolchain / IDE** | **`CMake`** | ⚠️ 必须选 CMake，选错会生成别的 IDE 的工程文件 |
| Firmware Package | `STM32Cube FW_H7 V1.13.0` | 见 2.1.1 的版本说明 |
| Minimum Heap Size | `0x400` | |
| Minimum Stack Size | `0x600` | |

> 💡 堆和栈的大小最终体现在链接脚本 `<工程根>/STM32H750xx_FLASH.ld` 里：
> ```ld
> _Min_Heap_Size = 0x400;   /* required amount of heap  */
> _Min_Stack_Size = 0x600;  /* required amount of stack */
> ```
> boot 本身几乎不用堆，0x400 足够；栈 0x600（1.5KB）对 boot 也绰绰有余。app 的栈由 app 自己的链接脚本决定，与这里无关。

📷 **【截图占位】** Project Manager → Project 页。
需体现：Project Name、Toolchain/IDE 下拉框选中 **CMake**、Firmware Package 版本、Heap/Stack 大小。

### 3.2.2 Code Generator 子页

| 选项 | 设置 | 说明 |
|---|---|---|
| **Copy only the necessary library files** | ✅ 勾选 | 只拷贝用到的 HAL 驱动，工程更干净 |
| Generate peripheral initialization as a pair of '.c/.h' files per peripheral | ✅ 勾选 | **本工程是勾选的** → 生成独立的 `gpio.c` / `quadspi.c` / `usart.c` |
| **Keep User Code when re-generating** | ✅ **必须勾选** | 保护 `/* USER CODE BEGIN */` 之间的代码，重新生成时不被覆盖 |
| Delete previously generated files when not re-generated | ✅ 勾选（默认） | |
| Backup previously generated files when re-generating | 可不勾 | 有 Git 就不需要 |

> ⚠️ **`Keep User Code when re-generating` 是生命线**。本工程第 7 章的引导流程全部写在 `/* USER CODE BEGIN 2 */` … `/* USER CODE END 2 */` 之间，靠这个选项才能在你以后修改外设配置、重新生成代码时不被抹掉。
> 判断是否勾对：生成后的 `Core/Src/main.c` 里应能看到 `/* USER CODE BEGIN 2 */` 和 `/* USER CODE END 2 */` 这对标记。

📷 **【截图占位】** Project Manager → Code Generator 页。
需体现：`Copy only the necessary library files`、`Generate peripheral initialization as a pair of .c/.h files per peripheral`、`Keep User Code when re-generating` 三项的勾选状态。

## 3.3 时钟配置（RCC + Clock Configuration）

### 3.3.1 RCC 设置

`System Core → RCC`：

| 项目 | 设置 |
|---|---|
| High Speed Clock (HSE) | **Crystal/Ceramic Resonator** |
| Low Speed Clock (LSE) | **Disable**（本工程不用 RTC / 32.768kHz） |
| Master Clock Output | Disable |
| LSCO Clock Output | Disable |

📷 **【截图占位】** System Core → RCC 面板。需体现 HSE = Crystal/Ceramic Resonator，LSE = Disable。

### 3.3.2 时钟树（Clock Configuration 标签页）

切到 `Clock Configuration` 标签页。**H750 的时钟树默认是灰色的**，需要先在最上方选择时钟源（`PLLCLK`），灰色才会解除 —— 如果某个输入框点不动，先检查左侧的 mux 选择。

按以下数值填（从 HSE 输入一路往右）：

| 位置 | 参数 | 值 | 结果 |
|---|---|---|---|
| 输入 | HSE 频率 | **25 MHz** | 板载晶振 |
| PLL Source Mux | PLL 时钟源 | **HSE** | |
| PLL1 | **DIVM1** | **5** | 25 ÷ 5 = **5 MHz**（VCO 输入） |
| PLL1 | **DIVN1** | **192** | 5 × 192 = **960 MHz**（VCO 输出） |
| PLL1 | **DIVP1** | **2** | 960 ÷ 2 = **480 MHz** → SYSCLK |
| PLL1 | DIVQ1 | 2 | 480 MHz（本工程未使用） |
| PLL1 | DIVR1 | 2 | 480 MHz（本工程未使用） |
| System Clock Mux | SYSCLK 源 | **PLLCLK** | **SYSCLK = 480 MHz** |
| AHB Prescaler | **HPRE** | **/2** | **HCLK = 240 MHz** |
| APB3 Prescaler | D1PPRE | **/2** | APB3 = 120 MHz |
| APB1 Prescaler | D2PPRE1 | **/2** | APB1 = 120 MHz |
| APB2 Prescaler | D2PPRE2 | **/2** | APB2 = 120 MHz |
| APB4 Prescaler | D3PPRE | **/2** | APB4 = 120 MHz |
| 电源 | **Power Regulator Voltage Scale** | **Scale 0** | 480MHz 必须 VOS0 |
| 电源 | Supply Source | **LDO** | 板上无 SMPS，只能用 LDO |
| Flash | Flash Latency | **4 WS**（自动算出） | 480MHz + VOS0 需要 4 等待周期 |
| **QSPI** | **QUADSPI 时钟 mux** | **HCLK**（D1HCLK） | **★ 关键，见下** |

> ⚠️ **QSPI 时钟 mux 一定要选 HCLK**。这一项很容易漏，因为它藏在时钟树右下角一带。如果漏了，`MX_QUADSPI_Init()` 里就不会有配置 QSPI 内核时钟的那两行，QSPI 会用一个很低的默认时钟，导致读取极慢甚至失败。
> 正确的生成结果见 4.2.2：`Core/Src/quadspi.c` 的 `HAL_QSPI_MspInit()` 里应有
> ```c
> PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
> PeriphClkInitStruct.QspiClockSelection  = RCC_QSPICLKSOURCE_D1HCLK;
> HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
> ```

📷 **【截图占位】** Clock Configuration 标签页全景。
需体现：HSE 25MHz 输入、PLL1 的 M/N/P、SYSCLK 显示 480MHz、HCLK 240MHz、四个 APB 都是 120MHz、Voltage Scale 选 Scale 0、以及 QUADSPI 那一行的 mux 选的是 HCLK。

💡 **原理：这些数字为什么长这样**

- **VCO 输入必须落在 1~16MHz**（H750 的 PLL 输入范围），25 ÷ 5 = 5MHz 落在区间中段，比较稳。
- **VCO 输出必须落在 192~960MHz**（宽范围模式），5 × 192 = 960MHz 正好是上限，这样 ÷2 得到 480MHz —— 这是 H750 的**最高主频**。
- **HCLK 必须 ≤ 240MHz**，所以 AHB 再 ÷2。注意：Cortex-M7 内核跑 480MHz，但 AXI/AHB 总线只有 240MHz，这是 H7 的架构特点（内核频率和总线频率分离）。
- **APB 最高 120MHz**，所以全部 ÷2。
- **Voltage Scale 0（VOS0）**：H7 有 VOS0/VOS1/VOS2/VOS3 四档，主频越高需要越高的内核电压。480MHz 必须用 VOS0，否则芯片根本跑不到这个频率（会直接不启动或跑飞）。VOS0 需要先配置供电方式（LDO 或 SMPS），本工程用 LDO。
- **Flash Latency 4**：内部 Flash 读取有物理延迟，主频越高需要插入越多等待周期。CubeMX 会根据你的主频和 VOS 自动算出 4，不用手填。**这个值只影响 boot 自己从内部 Flash 取指令**，app 从 QSPI 取指令是另一套时序（由 QSPI 的 dummy cycle 决定）。
- **QSPI 用 HCLK 作为内核时钟**：QSPI 内核时钟是"外设自己的工作时钟"，它会再经过 `ClockPrescaler` 分频变成真正的 SPI 驱动时钟。

## 3.4 GPIO 配置（LED）

`System Core → GPIO`。

### 3.4.1 PC13（LED1）

在引脚列表里找到 **PC13**，点击 → 选 **`GPIO_Output`**。然后在下方 `GPIO` 配置表里设置这一行：

| 列 | 值 |
|---|---|
| GPIO output level | **Low** |
| GPIO mode | **Output Push Pull** |
| GPIO Pull-up/Pull-down | **No pull-up and no pull-down** |
| Maximum output speed | **Low** |
| User Label | **`LED1`** |

> 💡 **User Label 填 `LED1` 会生成两个宏**，在 `Core/Inc/main.h` 里：
> ```c
> #define LED1_Pin       GPIO_PIN_13
> #define LED1_GPIO_Port GPIOC
> ```
> 第 5 章的 `BSP/Inc/led.h` 直接用这两个宏，所以 **Label 必须叫 `LED1`**（大小写敏感）。如果你改了 Label，`led.h` 里的宏也要跟着改。

> 💡 **Output level = Low 的含义**：CubeMX 会生成一句 `HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);`，也就是上电就把 PC13 拉低。本工程使用的核心板上 LED 是**低电平点亮**，所以效果是"一上电 LED 就亮"。

### 3.4.2 调试口 PA13 / PA14

这两个脚默认已被占用为 **Serial Wire**（SWD）。**保持不动**，不要禁用 —— 否则以后没法用 ST-Link 下载和调试。

📷 **【截图占位】** System Core → GPIO 面板。
需体现：PC13 那一行的 5 个设置项（Output level = Low、Mode = Output Push Pull、Pull = No pull、Speed = Low、User Label = LED1）。

✅ **已验证**：`.ioc` 里对应：
```
PC13.GPIOParameters=GPIO_Label
PC13.GPIO_Label=LED1
PC13.Signal=GPIO_Output
PA13 (JTMS/SWDIO).Mode=Serial_Wire
PA14 (JTCK/SWCLK).Mode=Serial_Wire
```

## 3.5 CORTEX_M7 缓存配置

`System Core → CORTEX_M7`：

| 项目 | 设置 | 生成的代码 |
|---|---|---|
| **CPU ICache** | **Enabled** | `SCB_EnableICache();` |
| **CPU DCache** | **Enabled** | `SCB_EnableDCache();` |

✅ **已验证**：生成的 `Core/Src/main.c` 里会有（本工程行号 82-86 附近）：

```c
  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */
  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();
  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();
```

💡 **原理：为什么 boot 要开缓存**

boot 的运行时间极短，开缓存对速度意义不大。本项目仍然开启缓存，是因为：**开启 I/D-Cache 能明显加快 boot 自身从 Flash 取指令的速度**；但**必须在跳转前把缓存清干净**，否则 app 会继承一个"装着 boot 数据"的缓存、读到脏数据。所以本工程在开启缓存的同时，在跳转前显式关闭（详见 [7.5](#75-为什么跳转前要关-icache--dcache)）。

## 3.6 MPU 区域 0：CubeMX 默认生成的"背景保护区"

### 3.6.1 区域 0 的生成结果

只要在 3.5 里启用了 Cache，CubeMX 就会在 `Core/Src/main.c` 里生成一个 `MPU_Config()` 函数，其中第一段就是区域 0：

```c
/* <工程根>/Core/Src/main.c 的 MPU_Config() —— 区域 0 部分（当前工程行号 231~243） */
void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  ...
}
```

这段配置的形式是"从 `0x0` 开始、覆盖 4GB、禁止访问"，它通过**"挖洞"**的方式只保护需要保护的部分。

### 3.6.2 含义：子区（SubRegion）机制

这块配置的来源是 `System Core → CORTEX_M7 → Speculation default mode`（`.ioc` 里的 `CORTEX_M7.default_mode_Activation=1`）。它的目的是**防止 Cortex-M7 的推测访问（speculation）跑到未定义的地址空间去**（这是 ARM 建议的做法，可参考 AN4838）。

关键在于 `SubRegionDisable = 0x87` 这个字段。一个 4GB 的区域被**平均切成 8 个子区**，每个 512MB。`SubRegionDisable` 的 8 个 bit 分别对应 8 个子区，**bit = 1 表示"这个子区不受本区域管辖"**（即交还给默认内存映射）：

`0x87 = 1000 0111b`

| bit | 值 | 对应子区 | 地址范围 | 结果 |
|---|---|---|---|---|
| 0 | 1 | SR0 | `0x0000_0000` ~ `0x1FFF_FFFF` | **排除**（默认映射，正常）|
| 1 | 1 | SR1 | `0x2000_0000` ~ `0x3FFF_FFFF` | **排除**（DTCM/AXI SRAM 正常）|
| 2 | 1 | SR2 | `0x4000_0000` ~ `0x5FFF_FFFF` | **排除**（外设寄存器正常）|
| 3 | 0 | SR3 | `0x6000_0000` ~ `0x7FFF_FFFF` | **受管辖 → 禁止访问** |
| 4 | 0 | SR4 | `0x8000_0000` ~ `0x9FFF_FFFF` | **受管辖 → 禁止访问** |
| 5 | 0 | SR5 | `0xA000_0000` ~ `0xBFFF_FFFF` | **受管辖 → 禁止访问** |
| 6 | 0 | SR6 | `0xC000_0000` ~ `0xDFFF_FFFF` | **受管辖 → 禁止访问** |
| 7 | 1 | SR7 | `0xE000_0000` ~ `0xFFFF_FFFF` | **排除**（系统区正常）|

也就是说，工程师的意图是：

> "内部 Flash（`0x08000000`，在 SR0）、SRAM（`0x20000000`，在 SR1）、外设（`0x40000000`，在 SR2）都正常放行；**只有 `0x60000000` ~ `0xDFFFFFFF` 这片'外部存储器区'默认不给访问**。"

设计初衷是"外部存储器没配置好之前别乱访问"。

### 3.6.3 与区域 1 的关系

区域 0 覆盖了 `0x90000000` 所在的子区（SR4）。要让 app 能从 QSPI 里取指令执行，就需要**再增加一个专门针对 QSPI 区域的配置** —— 也就是下一节的区域 1。ARM MPU 的规则是**区域编号越大优先级越高**，所以区域 1 的设置会覆盖区域 0 对这段地址的管辖。

这也是 ST 官方 QSPI bootloader 例程的做法 —— 参考 `STM32Cube_FW_H7_V1.13.0\Projects\STM32H743I-EVAL\Applications\ExtMem_CodeExecution\ExtMem_Boot\Src\main.c`：它先用区域 0 设背景保护区，紧接着加了一个 `BaseAddress = 0x90000000, Size = 128MB, FULL_ACCESS` 的区域。

## 3.7 MPU 区域 1：放行 QSPI 内存映射区

区域 0 把 `0x9000_0000` 所在的子区归入了"禁止访问"，所以还需要**再配置一个区域 1 来放行 QSPI 内存映射区**。本节给出这个区域的完整配置。

### 3.7.1 配置位置

`System Core → CORTEX_M7` 面板，往下滚动，找到分组：

**`Cortex Memory Protection Unit Region 1 Settings`**

📷 **【截图占位】** CubeMX CORTEX_M7 面板中的 `Cortex Memory Protection Unit Region 1 Settings` 分组。
需体现全部 11 个参数以及它们被填好的值（尤其是 **AccessPermission = ALL ACCESS PERMITTED** 这一项必须可见）。

### 3.7.2 11 个参数

| 参数（CubeMX 界面名） | 值 | 说明 |
|---|---|---|
| Enable | `Enabled` | 启用本区域 |
| Number | `MPU_REGION_NUMBER1` | CubeMX 自动填写（编号越大优先级越高）|
| BaseAddress | `0x90000000` | QSPI Bank 1 的内存映射起始地址 |
| Size | `128MBytes` | 覆盖整个 W25Q64（8MB）并有充裕余量 |
| SubRegionDisable | `0x0` | 不使用子区禁用，整个区域统一属性 |
| TypeExtField | `TEX Level 0` | 与 C/B 位组合出 Normal 内存类型 |
| **AccessPermission** | **`ALL ACCESS PERMITTED`** | 允许读写 —— **本项要选 "ALL ACCESS PERMITTED"** |
| DisableExec | `INSTRUCTION ACCESS ENABLED` | 允许从该区域取指令（app 的代码就在这里）|
| IsShareable | `NOT SHAREABLE` | 非共享 |
| IsCacheable | `CACHEABLE` | 可缓存 |
| IsBufferable | `NOT BUFFERABLE` | 与 C=1 组合成 Write-Through |

### 3.7.3 生成结果与自查

配置正确时，`Core/Src/main.c` 的 `MPU_Config()` 里区域 1 部分是这样的（本工程的实际状态）：

```c
/* <工程根>/Core/Src/main.c 的 MPU_Config() —— 区域 1 部分（当前工程行号 247~256） */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   /* ← 访问权限 */
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
```

同时 `.ioc` 里会出现下面这个键 —— **这是最可靠的自查方法**：

```
CORTEX_M7.AccessPermission_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_REGION_FULL_ACCESS
```

### 3.7.4 区域 1 属性的含义

| 属性 | 值 | 含义 |
|---|---|---|
| 地址范围 | `0x9000_0000` ~ `0x97FF_FFFF` | 128MB，完整覆盖 W25Q64 的 8MB 还多 |
| 访问权限 | FULL_ACCESS | 读写都可以 |
| 可执行 | DisableExec = ENABLE | **很重要**：app 的代码就在这片区域，必须允许取指令 |
| 缓存属性 | TEX=0, C=1, B=0, S=0 | Normal memory、非共享、**写直达（Write-Through）** |
| 优先级 | 区域编号 1 > 区域 0 | ARM MPU 规则：**编号大的区域优先级高**，所以区域 1 能覆盖区域 0 的 NO_ACCESS |

> 💡 **对齐要求**：ARM 规定区域的基地址必须按区域大小对齐。128MB 区域的基地址必须是 128MB 的倍数 —— `0x90000000 ÷ 0x08000000 = 18`，正好整除 ✓；若把 Size 改成 64MB，`0x90000000 ÷ 0x04000000 = 36` 同样整除，也合法。

> 💡 **访问权限与缓存属性的关系**：`AccessPermission` 管的是"能不能访问"，`IsCacheable/IsBufferable/TypeExtField` 管的是"访问时走不走缓存"。两者独立。这里把 QSPI 区设为 Write-Through，是因为该区域只读（内存映射模式下不能写），设成 Write-Through 可以避免写分配的额外开销。

## 3.8 QUADSPI 配置

### 3.8.1 找到外设

左侧 `Categories → Connectivity → QUADSPI`。

| 项目 | 选择 |
|---|---|
| Mode | **`Bank1 with Quad SPI lines`**（单 Bank 1、四线） |

选中后 CubeMX 会自动把 6 根引脚分配到芯片上。**必须确认分配到的引脚与 1.5.2 表完全一致**（如果板子走线不同，这里会不一样，需要手动改）：

```
PG6   ------> QUADSPI_BK1_NCS
PF6   ------> QUADSPI_BK1_IO3
PF7   ------> QUADSPI_BK1_IO2
PF8   ------> QUADSPI_BK1_IO0
PF9   ------> QUADSPI_BK1_IO1
PF10  ------> QUADSPI_CLK
```

📷 **【截图占位】** QUADSPI 的引脚分配视图（Pinout view）。
需体现：PF6/PF7/PF8/PF9/PF10/PG6 六个引脚被标记为 QUADSPI 功能。

### 3.8.2 参数设置（Parameter Settings）

在下方 `Parameter Settings` 里填写。**这 8 个值必须逐一对上**：

| 参数 | 值 | 对应的 HAL 初始化字段 |
|---|---|---|
| **Clock Prescaler** | **`1`** | `hqspi.Init.ClockPrescaler = 1;` |
| **FIFO Threshold** | **`32`** | `hqspi.Init.FifoThreshold = 32;` |
| **Sample Shifting** | **`Half Cycle`** | `QSPI_SAMPLE_SHIFTING_HALFCYCLE` |
| **Flash Size** | **`22`** | `hqspi.Init.FlashSize = 22;` |
| **Chip Select High Time** | **`1 Cycle`** | `QSPI_CS_HIGH_TIME_1_CYCLE` |
| **Clock Mode** | **`Mode 3`** | `QSPI_CLOCK_MODE_3` |
| **Flash ID** | **`Bank 1`** | `QSPI_FLASH_ID_1` |
| **Dual Flash** | **`Disabled`** | `QSPI_DUALFLASH_DISABLE` |

**逐条解释**：

| 参数 | 为什么是这个值 |
|---|---|
| Clock Prescaler = 1 | QSPI 驱动时钟 = 内核时钟 ÷ (1+1)。内核时钟 = HCLK = 240MHz → **驱动时钟 = 120MHz**。W25Q64JV 最高支持 133MHz，120MHz 留了余量。内存映射模式下该值需要 ≥ 1 |
| FIFO Threshold = 32 | FIFO 阈值，间接模式下攒够 32 字节才触发传输。取最大值可减少中断/查询次数 |
| Sample Shifting = Half Cycle | **采样点后移半个时钟周期**。120MHz 下信号在板子上走线会有延迟，延后半周期采样更可靠。这是 ST 对高速 QSPI 的推荐做法 |
| Flash Size = 22 | Flash 容量 = 2^(22+1) = **8MB**，与 W25Q64 的容量一致。该值要与芯片实际容量匹配 |
| Chip Select High Time = 1 Cycle | 两条命令之间 nCS 至少保持 1 个时钟周期的高电平（120MHz 下约 8.3ns）。CubeMX 这个字段的语义是"至少 1 个周期"，本工程实测通信正常 |
| Clock Mode = Mode 3 | SPI 模式 3（CPOL=1, CPHA=1，时钟空闲为高，第二个边沿采样）。**W25Q64 同时支持 Mode 0 和 Mode 3**，本工程使用 Mode 3 |
| Flash ID = Bank 1 | 使用 Bank 1（对应地址 `0x90000000`）。只有双 Flash 模式才需要区分 |
| Dual Flash = Disabled | 板上只有一片 Flash，不用双闪存模式 |

### 3.8.3 引脚速度设置

QSPI 的驱动时钟是 **120MHz**，所以这 6 根引脚的 `Maximum output speed` 要设为 **`Very High`**。

在 `System Core → GPIO` 面板里，把这 **6 根引脚**的 `Maximum output speed` 全部设为 **`Very High`**：

```
PF6   → Very High
PF7   → Very High
PF8   → Very High
PF9   → Very High
PF10  → Very High
PG6   → Very High
```

> ✅ **参照依据**：ST 官方 QSPI bootloader 例程 `ExtMem_Boot\Src\memory_msp.c` 里，QSPI 的 CLK / CS / D0~D3 全部使用 `GPIO_SPEED_FREQ_VERY_HIGH`。本工程照此设置。

📷 **【截图占位】** System Core → GPIO 面板。
需体现：PF6 / PF7 / PF8 / PF9 / PF10 / PG6 六行的 `Maximum output speed` 全部是 **Very High**。

✅ **已验证**：`.ioc` 里每根脚都应出现 `GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH`：

```
PF10.GPIOParameters=GPIO_Speed
PF10.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF10.Mode=Single Bank 1
PF10.Signal=QUADSPI_CLK
（PF6 / PF7 / PF8 / PF9 / PG6 同样）
```

对应生成的代码 `Core/Src/quadspi.c` 里三处 `HAL_GPIO_Init` 都应是 `GPIO_SPEED_FREQ_VERY_HIGH`（见 4.2.2 核对）。

## 3.9 USART1 配置（串口打印）

`Categories → Connectivity → USART1`：

| 项目 | 设置 |
|---|---|
| Mode | **`Asynchronous`**（异步） |
| Hardware Flow Control | `Disable` |

引脚自动分配到 **PA9 = USART1_TX、PA10 = USART1_RX**。

### 3.9.1 Parameter Settings

| 参数 | 值 |
|---|---|
| Baud Rate | **115200** Bits/s |
| Word Length | **8 Bits**（含校验位） |
| Parity | **None** |
| Stop Bits | **1** |
| Data Direction | **Receive and Transmit** |
| Over Sampling | **16 Samples** |
| **Clock Prescaler** | **`DIV1`** |
| **FIFO Mode** | **`Disabled`** ← ⚠️ 必须关 |
| Tx FIFO Threshold | `1/8`（默认） |
| Rx FIFO Threshold | `1/8`（默认） |

### 3.9.2 Advanced Features

`Advanced Features` 分组：

| 参数 | 值 |
|---|---|
| Auto Baudrate | Disabled |
| **Overrun** | **`Overrun Disable`**（保持默认） |
| **DMA on RX Error** | **`DMA Disable on RX Error`**（保持默认） |
| MSB First | Disabled |
| Data Inversion / TX-RX Pin Swap 等 | 全部 Disabled |

> 💡 **关于 FIFO Mode = Disabled**：H7 的串口带硬件 FIFO。本工程关闭它，并把收/发 FIFO 阈值都设为 1/8，这样串口行为更接近传统串口，便于使用最朴素的阻塞式收发。生成的代码是这几句：`HAL_UARTEx_SetTxFifoThreshold(...)`、`HAL_UARTEx_SetRxFifoThreshold(...)`、`HAL_UARTEx_DisableFifoMode(...)`。

> 💡 **关于 Overrun / DMA on RX Error**：CubeMX 默认会打开"接收溢出不报错"和"接收错误时禁用 DMA"这两项。对本工程**无影响**，因为 boot 只用串口**发送**，根本不接收数据。

### 3.9.3 NVIC 不用开中断

**USART1 global interrupt 保持不勾选。**

💡 **原理**：本工程的串口发送用的是**阻塞式** API `HAL_UART_Transmit()`（在 `Library/Src/retarget.c` 里），它会轮询发送寄存器状态，不需要中断。开了中断反而要写中断服务函数，徒增代码量。

📷 **【截图占位】** USART1 的 Parameter Settings 面板。
需体现：115200 / 8 Bits / None / 1 / Receive and Transmit / 16 Samples / Clock Prescaler = DIV1 / **FIFO Mode = Disabled**。

## 3.10 NVIC 设置：一个中断都不用开

`System Core → NVIC`：

**除了默认的 `SysTick`（HAL 时基）和几个内核异常（HardFault / MemManage / BusFault / UsageFault / SVC / PendSV / NMI），不要勾选任何外设中断。**

本工程涉及的两个外设 QUADSPI 和 USART1 都使用**轮询方式**：

| 外设 | 本工程用的 API | 需要中断吗 |
|---|---|---|
| QUADSPI | `HAL_QSPI_Command()` / `HAL_QSPI_AutoPolling()` / `HAL_QSPI_Receive()` / `HAL_QSPI_MemoryMapped()` | ❌ 不需要 |
| USART1 | `HAL_UART_Transmit()` | ❌ 不需要 |

✅ **已验证**：`Core/Src/stm32h7xx_it.c` 里只有内核异常处理函数，没有 `QUADSPI_IRQHandler` / `USART1_IRQHandler`。

> 💡 这样做的另一个好处：**boot 不使用任何外设中断，跳转前就不需要关闭 NVIC**，只需停掉 SysTick 即可（见 [第 7 章](#第-7-章-引导流程实现)）。

## 3.11 生成代码

配置完成后，点右上角的 **`GENERATE CODE`**。

第一次生成时 CubeMX 会提示 "The project is not fully configured, ... do you want to generate?"（因为没配时钟之类的警告），确认即可。

生成完成后：

| 动作 | 说明 |
|---|---|
| `Open Project` | 用默认 IDE 打开。本工程用 CLion 或命令行 |
| `Close` | 只生成，稍后自己打开 |

📷 **【截图占位】** 生成完成的提示窗口 + 生成的目录结构（资源管理器中展开 `Core/Src`、`BSP`、`Library`、`cmake`）。

生成后应先对照 **第 4 章** 逐项核对，确认无误再往下写代码。

## 3.12 `.ioc` 关键参数速查表

下面这张表是你配置完之后 `STM32H750XBH6_Bootloader.ioc` 里**必须出现**的关键行。用它可以在不打开 CubeMX 的情况下快速自查配置是否正确。完整 `.ioc` 见 [附录 D](#附录-d-ioc-文件全文)。

| 类别 | 关键行 | 对应章节 |
|---|---|---|
| 芯片 | `Mcu.CPN=STM32H750XBH6` / `Mcu.Package=TFBGA240` | 3.1 |
| 工具链 | `ProjectManager.TargetToolchain=CMake` | 3.2.1 |
| 固件包 | `ProjectManager.FirmwarePackage=STM32Cube FW_H7 V1.13.0` | 3.2.1 |
| 堆栈 | `ProjectManager.HeapSize=0x400` / `ProjectManager.StackSize=0x600` | 3.2.1 |
| 时钟 | `RCC.DIVM1=5` / `RCC.DIVN1=192` / `RCC.HPRE=RCC_HCLK_DIV2` | 3.3 |
| 时钟 | `RCC.SYSCLKFreq_VALUE=480000000` / `RCC.HCLKFreq_Value=240000000` | 3.3 |
| QSPI 时钟 | `RCC.QSPIFreq_Value=240000000` | 3.3.2 |
| GPIO | `PC13.GPIO_Label=LED1` / `PC13.Signal=GPIO_Output` | 3.4 |
| Cache | `CORTEX_M7.CPU_ICache=Enabled` / `CORTEX_M7.CPU_DCache=Enabled` | 3.5 |
| MPU | `CORTEX_M7.default_mode_Activation=1` | 3.6 |
| **MPU** | **`CORTEX_M7.AccessPermission_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_REGION_FULL_ACCESS`** | **3.7 ★** |
| MPU | `CORTEX_M7.BaseAddress_S-...=0x90000000` | 3.7 |
| MPU | `CORTEX_M7.Size_S-...=MPU_REGION_SIZE_128MB` | 3.7 |
| MPU | `CORTEX_M7.IsCacheable_S-...=MPU_ACCESS_CACHEABLE` | 3.7 |
| QSPI | `QUADSPI.ClockPrescaler=1` / `QUADSPI.FifoThreshold=32` / `QUADSPI.FlashSize=22` | 3.8.2 |
| QSPI | `QUADSPI.SampleShifting=QSPI_SAMPLE_SHIFTING_HALFCYCLE` / `QUADSPI.ClockMode=QSPI_CLOCK_MODE_3` | 3.8.2 |
| **QSPI 引脚** | **`PF6.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH`**（PF7/PF8/PF9/PF10/PG6 同样） | **3.8.3 ★** |
| USART1 | `USART1.VirtualMode-Asynchronous=VM_ASYNC`（+ 波特率等在生成代码里） | 3.9 |
| 调试 | `PA13 (JTMS/SWDIO).Mode=Serial_Wire` / `PA14 (JTCK/SWCLK).Mode=Serial_Wire` | 3.4.2 |

---

# 第 4 章 生成代码核对清单

CubeMX 生成完代码后，**先别急着写业务代码**，花十分钟对照本章核对一遍。本章列出的每一项都是"错了就一定跑不通、但错了又不容易发现"的点。

## 4.1 生成的文件树

✅ **已验证**：CubeMX 生成 + 后续手工添加 BSP/Library 之后，工程应长这样（省略 `Drivers/` 里的库文件）：

```
STM32H750XBH6_Bootloader/
├── STM32H750XBH6_Bootloader.ioc      ← CubeMX 工程配置存档
├── CMakeLists.txt                    ← 顶层构建脚本（CubeMX 只生成一次，可改）
├── CMakePresets.json                 ← 预设（Debug/Release）
├── STM32H750xx_FLASH.ld              ← 链接脚本（★ 地址布局在这里）
├── startup_stm32h750xx.s             ← 启动文件（GNU 汇编语法）
├── .mxproject                        ← CubeMX 辅助文件
├── cmake/
│   ├── gcc-arm-none-eabi.cmake       ← GCC 工具链定义
│   ├── starm-clang.cmake             ← ST Clang 工具链定义（本工程不用）
│   └── stm32cubemx/
│       └── CMakeLists.txt            ← ★ CubeMX 每次重新生成都会重写这个文件
├── Core/
│   ├── Inc/
│   │   ├── main.h                    ← ★ LED1 引脚宏在这里
│   │   ├── gpio.h
│   │   ├── quadspi.h                 ← 声明 extern QSPI_HandleTypeDef hqspi
│   │   ├── usart.h                   ← 声明 extern UART_HandleTypeDef huart1
│   │   ├── stm32h7xx_hal_conf.h      ← ★ HAL 模块开关
│   │   ├── stm32h7xx_it.h
│   │   └── stm32h7xx_hal_conf.h
│   └── Src/
│       ├── main.c                    ← ★ 主程序（引导流程写在这里的 USER CODE 段）
│       ├── gpio.c                    ← PC13 初始化
│       ├── quadspi.c                 ← ★ QUADSPI 初始化
│       ├── usart.c                   ← USART1 初始化
│       ├── stm32h7xx_it.c            ← 中断/异常处理
│       ├── stm32h7xx_hal_msp.c       ← 全局 MSP（只有 HAL_MspInit）
│       ├── syscalls.c                ← newlib 系统调用桩（★ 要改）
│       ├── sysmem.c                  ← _sbrk 实现（堆管理）
│       └── system_stm32h7xx.c        ← SystemInit 等
├── BSP/                              ← ★ 手写：板级支持包
│   ├── CMakeLists.txt
│   ├── Inc/
│   │   ├── led.h                     ← LED 控制宏（纯头文件）
│   │   └── qspi_w25q64.h             ← W25Q64 驱动接口
│   └── Src/
│       └── qspi_w25q64.c             ← ★ W25Q64 驱动实现
├── Library/                          ← ★ 手写：printf 重定向库
│   ├── CMakeLists.txt
│   ├── Inc/retarget.h
│   └── Src/retarget.c
└── Drivers/                          ← ST 官方库（不要改）
    ├── CMSIS/
    └── STM32H7xx_HAL_Driver/
```

> 带 ★ 的是本章要核对的文件。

## 4.2 逐文件核对点

### 4.2.1 `Core/Inc/main.h` —— LED 引脚宏

**核对点**：文件末尾附近应有这两行，**名称必须是 `LED1_Pin` / `LED1_GPIO_Port`**（第 5 章 `BSP/Inc/led.h` 依赖这两个名字）：

```c
/* <工程根>/Core/Inc/main.h 末尾 */
/* Private defines -----------------------------------------------------------*/
#define LED1_Pin GPIO_PIN_13
#define LED1_GPIO_Port GPIOC
```

| 检查 | 期望 |
|---|---|
| 有 `LED1_Pin` | 否则回 3.4.1 检查 PC13 的 `User Label` 是否填了 `LED1` |

### 4.2.2 `Core/Src/quadspi.c` —— QUADSPI 初始化（★ 重点）

这个文件要核对**三块内容**。

**第一块：8 个初始化参数**（`MX_QUADSPI_Init()`，当前工程行号 40~48）

```c
/* <工程根>/Core/Src/quadspi.c:40-52 */
  hqspi.Instance = QUADSPI;
  hqspi.Init.ClockPrescaler = 1;
  hqspi.Init.FifoThreshold = 32;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize = 22;
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_3;
  hqspi.Init.FlashID = QSPI_FLASH_ID_1;
  hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler();
  }
```

**第二块：QSPI 内核时钟**（`HAL_QSPI_MspInit()` 里，当前工程行号 72~77）—— 这是 3.3.2 里选 HCLK 的结果：

```c
/* <工程根>/Core/Src/quadspi.c:70-77 */
  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
```

| 检查 | 期望 |
|---|---|
| 有 `RCC_PERIPHCLK_QSPI` | 必须有（QSPI 内核时钟配置）|
| 有 `RCC_QSPICLKSOURCE_D1HCLK` | 必须有（内核时钟源 = HCLK = 240MHz）|

**第三块：6 根引脚 + 速度必须是 `VERY_HIGH`**（当前工程行号 92~111）

```c
/* <工程根>/Core/Src/quadspi.c:92-111 */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* ← 必须是 VERY_HIGH */
    GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);                /* PG6  = NCS */

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* ← 必须是 VERY_HIGH */
    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);                /* PF6/PF7/PF10 (AF9) */

    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* ← 必须是 VERY_HIGH */
    GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);                /* PF8/PF9 (AF10) */
```

| 检查 | 期望 |
|---|---|
| 三处 `Speed` 都是 `GPIO_SPEED_FREQ_VERY_HIGH` | **必须**（对应 120MHz 驱动时钟）|
| 复用号：PF6/PF7/PF10 = **AF9**，PF8/PF9/PG6 = **AF10** | 必须 |
| GPIO 端口 | PG6 在 **GPIOG**，其余 5 根在 **GPIOF** |

> 一句话自检命令（Linux/macOS 风格 grep，Windows 上用 `findstr`）：
> ```bat
> findstr /N "GPIO_SPEED_FREQ" Core\Src\quadspi.c
> ```
> 应输出 **3 行**，且全部是 `VERY_HIGH`。

### 4.2.3 `Core/Src/usart.c` —— USART1 初始化

```c
/* <工程根>/Core/Src/usart.c:41-53 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
```

FIFO 关闭的四句（当前工程行号 58~69）：

```c
/* <工程根>/Core/Src/usart.c:58-69 */
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) { Error_Handler(); }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) { Error_Handler(); }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK) { Error_Handler(); }
```

引脚配置（当前工程行号 104~109）：

```c
/* <工程根>/Core/Src/usart.c:100-109 */
    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    PA9      ------> USART1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

| 检查 | 期望 | 备注 |
|---|---|---|
| 波特率 115200 | ✓ | 上位机串口终端也要设成 115200 |
| 有 `HAL_UARTEx_DisableFifoMode` | ✓ | 对应 [3.9.2](#392-advanced-features) 里的 FIFO Mode = Disabled |
| `GPIO_AF7_USART1` | ✓ | PA9/PA10 的串口复用号是 AF7 |
| USART1 时钟源 | `RCC_USART16CLKSOURCE_D2PCLK2` | 生成在 `HAL_UART_MspInit()` 里，= 120MHz |
| `Speed = GPIO_SPEED_FREQ_LOW` | **这里 LOW 是可以的** | 115200 波特率不需要高速档；与 QSPI 的情况不同，别改错对象 |

### 4.2.4 `Core/Src/gpio.c` —— PC13（LED）

```c
/* <工程根>/Core/Src/gpio.c:41-62 */
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED1_Pin */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED1_GPIO_Port, &GPIO_InitStruct);
}
```

| 检查 | 期望 |
|---|---|
| 有 `HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)` | 上电即点亮 LED（低电平点亮）|
| 有 `__HAL_RCC_GPIOC_CLK_ENABLE()` | 少了 → LED 不动 |

> 💡 **这就是"为什么 BSP 里不再写 `LED_Init()`"的原因**：PC13 的时钟、模式、上下拉、速度、初始电平，CubeMX 全都在这里配好了。BSP 只需要提供 `LED1_ON` / `LED1_OFF` / `LED1_Toggle` 这样的**语义宏**（第 5.2 节）。这也是本工程的职责划分原则：**外设配置归 CubeMX，CubeMX 生成不了的东西才手写。**

### 4.2.5 `Core/Src/main.c` —— Cache 与 MPU

**Cache 使能**（当前工程行号 78~91 附近）：

```c
/* <工程根>/Core/Src/main.c */
  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */
  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();
  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
```

**MPU 两个区域**（`MPU_Config()`，当前工程行号 223~259）—— 本节是**核对的重中之重**：

```c
/* <工程根>/Core/Src/main.c:231-243 —— 区域 0：背景保护区 */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.Number = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress = 0x0;
    MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
    MPU_InitStruct.SubRegionDisable = 0x87;
    ...
    MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
    ...
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

/* <工程根>/Core/Src/main.c:247-256 —— 区域 1：放行 QSPI 区（★ 必须完整） */
    MPU_InitStruct.Number = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress = 0x90000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_128MB;
    MPU_InitStruct.SubRegionDisable = 0x0;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   /* ← 访问权限 */
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);
```

| 检查 | 期望 |
|---|---|
| 有**两个** `HAL_MPU_ConfigRegion()` 调用 | 必须有 2 次（区域 0 + 区域 1）|
| 区域 1 段里有 `MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;` | **必须有** |
| 区域 1 段里有 `MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;` | 必须有 |
| 末尾有 `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);` | 必须有 |

### 4.2.6 `Core/Inc/stm32h7xx_hal_conf.h` —— HAL 模块开关

✅ **已验证**：用到了 QUADSPI 和 UART，所以这两个模块的开关必须是打开的：

```c
/* <工程根>/Core/Inc/stm32h7xx_hal_conf.h:67 */
#define HAL_QSPI_MODULE_ENABLED

/* <工程根>/Core/Inc/stm32h7xx_hal_conf.h:78 */
#define HAL_UART_MODULE_ENABLED
```

| 检查 | 期望 |
|---|---|
| `HAL_QSPI_MODULE_ENABLED` 未被注释 | 必须有 |
| `HAL_UART_MODULE_ENABLED` 未被注释 | 必须有 |

> 💡 这两个宏是 CubeMX 根据"你启用了哪些外设"自动加的。只要在 CubeMX 里启用了 QUADSPI 和 USART1，它们就会出现，**不需要手动改这个文件**。

### 4.2.7 `cmake/stm32cubemx/CMakeLists.txt` —— 源文件列表

⚠️ **这个文件 CubeMX 每次生成都会重写**，所以不要手改，但要核对它有没有把新文件加进来。

✅ **已验证**：应包含以下条目：

```cmake
# <工程根>/cmake/stm32cubemx/CMakeLists.txt:22-30
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/quadspi.c     # ← 第 25 行
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usart.c        # ← 第 26 行
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32h7xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32h750xx.s
)

# <工程根>/cmake/stm32cubemx/CMakeLists.txt:53-58（HAL 驱动）
    .../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c        # ← 第 53 行
    .../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c        # ← 第 57 行
    .../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c     # ← 第 58 行
```

| 检查 | 期望 |
|---|---|
| `quadspi.c` 在列表里 | 必须有 |
| `usart.c` 在列表里 | 必须有 |
| `stm32h7xx_hal_qspi.c` 在驱动列表里 | 必须有 |
| `stm32h7xx_hal_uart.c` + `stm32h7xx_hal_uart_ex.c` | 必须有 |

### 4.2.8 `STM32H750xx_FLASH.ld` —— 链接脚本

```ld
/* <工程根>/STM32H750xx_FLASH.ld:58-63 */
DTCMRAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
RAM (xrw)      : ORIGIN = 0x24000000, LENGTH = 512K
RAM_D2 (xrw)   : ORIGIN = 0x30000000, LENGTH = 288K
RAM_D3 (xrw)   : ORIGIN = 0x38000000, LENGTH = 64K
ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
FLASH (rx)     : ORIGIN = 0x8000000, LENGTH = 128K
```

| 检查 | 期望 | 说明 |
|---|---|---|
| `FLASH ... ORIGIN = 0x8000000, LENGTH = 128K` | **必须** | boot 从 `0x08000000` 开始；长度必须是 128K（H750 内部 Flash 就这么多）|
| `_Min_Heap_Size = 0x400` / `_Min_Stack_Size = 0x600` | 与 3.2.1 一致 | |
| `_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM)` | | 栈顶在 DTCM 末尾 `0x20020000` |

### 4.2.9 `Core/Src/stm32h7xx_it.c` —— 中断向量

| 检查 | 期望 | 说明 |
|---|---|---|
| 有 `MemManage_Handler` / `HardFault_Handler` / `BusFault_Handler` 等内核异常 | 必须有 | 出错时至少能停住便于调试 |
| **没有** `QUADSPI_IRQHandler` / `USART1_IRQHandler` | 应该没有 | 本工程不用中断；如果出现了，说明 3.10 里误勾了 NVIC |
| `MemManage_Handler` 里是 `while (1)` | 当前如此 | 这是 CubeMX 默认；意味着 MPU 违规会**死循环**而不是复位，便于用调试器抓现场 |

✅ **已验证**：本工程 `Core/Src/stm32h7xx_it.c` 中 `MemManage_Handler()` 在第 99 行，`while (1)` 在第 104 行。

## 4.3 用命令行验证"参数真的进了二进制"

核对源码还不够踏实？可以直接反汇编确认。下面三条命令在 **STM32CubeCLT 的命令行**里执行（先把 `...\GNU-tools-for-STM32\bin` 加进 PATH，见第 2 章）。

假设编译产物是 `build/Debug/STM32H750XBH6_Bootloader.elf`。

### 4.3.1 看内存占用

```bat
arm-none-eabi-size build\Debug\STM32H750XBH6_Bootloader.elf
```

✅ **已验证**：本工程的实测结果（详见 [9.1](#91-构建产物与内存占用)）：

| 构建类型 | FLASH 占用 | 内部 Flash 用量 |
|---|---|---|
| Debug（`-O0 -g3`） | 30824 B | 128 KB 的 **23.52%** |
| Release（`-Os -g0`） | 19364 B | 128 KB 的 **14.77%** |

### 4.3.2 确认 MPU 区域 1 真的配置对了（★ 强烈建议做一次）

**先用 Debug 构建**（`-O0` 下 `MPU_Config()` 不会被内联，是独立函数，最好读）：

```bat
arm-none-eabi-nm build\Debug\STM32H750XBH6_Bootloader.elf | findstr MPU_Config
```

✅ **已验证**：输出类似（`t` 表示局部符号、地址可能不同）：

```
080009dc t MPU_Config
```

然后反汇编这个函数（把地址填成上面查到的）：

```bat
arm-none-eabi-objdump -d --start-address=0x080009dc --stop-address=0x08000a5c ^
    build\Debug\STM32H750XBH6_Bootloader.elf
```

✅ **已验证**：本工程的关键几行（结构体字段偏移：0=Enable, 1=Number, 4=BaseAddress, 8=Size, 9=SubRegionDisable, 10=TEX, 11=AccessPermission, 12=DisableExec, 13=IsShareable, 14=IsCacheable, 15=IsBufferable）：

```asm
; ---- 区域 0 ----
 80009f2:  strb r3,[r7,#0]   r3=1        → Enable = ENABLE
 80009f6:  strb r3,[r7,#1]   r3=0        → Number = 0
 80009fc:  str  r3,[r7,#4]   r3=0        → BaseAddress = 0x0
 80009fe:  strb r3,[r7,#8]   r3=31(0x1F) → Size = 4GB
 8000a02:  strb r3,[r7,#9]   r3=135(0x87)→ SubRegionDisable = 0x87
 8000a0a:  strb r3,[r7,#11]  r3=0        → AccessPermission = NO_ACCESS
 8000a22:  bl   HAL_MPU_ConfigRegion

; ---- 区域 1 ----
 8000a26:  strb r3,[r7,#1]   r3=1        → Number = 1
 8000a2a:  mov.w r3,#0x90000000
 8000a2e:  str  r3,[r7,#4]               → BaseAddress = 0x90000000
 8000a30:  strb r3,[r7,#8]   r3=26(0x1A) → Size = 128MB
 8000a36:  strb r3,[r7,#9]   r3=0        → SubRegionDisable = 0
 8000a38:  strb r3,[r7,#11]  r3=3        → AccessPermission = FULL_ACCESS
 8000a3c:  strb r3,[r7,#12]  r3=0        → DisableExec = 可执行
 8000a40:  strb r3,[r7,#13]  r3=0        → IsShareable = NOT_SHAREABLE
 8000a44:  strb r3,[r7,#14]  r3=1        → IsCacheable = CACHEABLE
 8000a4c:  bl   HAL_MPU_ConfigRegion
 8000a50:  movs r0,#4
 8000a52:  bl   HAL_MPU_Enable            ← 4 = MPU_PRIVILEGED_DEFAULT
```

**读懂这段的关键**：
1. 区域 1 里**只出现了 8 条赋值**，没有 `Enable`、`TypeExtField`、`IsBufferable` —— 这不是遗漏，而是**沿用区域 0 已经写进结构体的值**。区域 0 的值恰好就是区域 1 需要的（`ENABLE` / `TEX0` / `NOT_BUFFERABLE`），所以正确。
2. **核对要点**：区域 1 段落里应有 `strb r3,[r7,#11]`，且写入的值是 `r3=3`（3 = FULL_ACCESS）。

> 💡 **为什么 Release 构建不好验证**：`-Os` 下 `MPU_Config()` 只被调用一次，GCC 会把它**内联进 `main()`**，`nm` 里就找不到这个符号了（这是正常现象，不是编译错误，`--gc-sections` 也不会把它删掉，因为它确实被调用了）。验证请用 Debug 构建。

### 4.3.3 确认 QSPI 参数

```bat
arm-none-eabi-objdump -d build\Debug\STM32H750XBH6_Bootloader.elf | findstr /C:"MX_QUADSPI_Init"
```

进去以后关注三件事：`movs r3,#1`（`ClockPrescaler = 1`）、`movs r3,#22`（`FlashSize = 22`，注意 22 是十进制立即数）、以及 `MX_QUADSPI_Init` 里对 `HAL_QSPI_Init` 的调用。

---

# 第 5 章 BSP 层：W25Q64 驱动

## 5.1 职责划分原则（本章的指导思想）

在动手写 BSP 之前，先把一条原则说清楚，它决定了"什么代码该放在哪"：

> **所有"外设配置"由 CubeMX 生成；只有 CubeMX 生成不了的代码才手写进 BSP。**

按这条原则划分的结果：

| 内容 | 归谁 | 为什么 |
|---|---|---|
| QUADSPI 的 8 个参数、内核时钟、6 根引脚（含速度、复用号、上下拉） | **CubeMX** → `Core/Src/quadspi.c` | 这些就是"外设寄存器怎么配"，CubeMX 天生就会 |
| QSPI 句柄 `hqspi` 的定义 | **CubeMX** → `Core/Src/quadspi.c` | 同上 |
| 复位 Flash、读 JEDEC ID、写使能、扇区/块擦除、页编程、进入内存映射模式 | **手写** → `BSP/Src/qspi_w25q64.c` | 这些是**目标芯片的命令协议**，CubeMX 生成不了 |
| PC13 的时钟/模式/速度/初始电平 | **CubeMX** → `Core/Src/gpio.c` | 就是 GPIO 配置 |
| `LED1_ON` / `LED1_OFF` / `LED1_Toggle` 这些"语义" | **手写** → `BSP/Inc/led.h` | CubeMX 不知道你这块板子的 LED 是高电平亮还是低电平亮 |

💡 **原理：为什么"芯片命令协议"必须手写**

看 HAL 里 QUADSPI 的初始化结构体定义 —— 它只有 8 个字段，全是"外设寄存器怎么配"：

```c
/* <工程根>/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_qspi.h */
typedef struct
{
  uint32_t ClockPrescaler;      /* 时钟分频 */
  uint32_t FifoThreshold;       /* FIFO 阈值 */
  uint32_t SampleShifting;      /* 采样点 */
  uint32_t FlashSize;           /* Flash 容量 */
  uint32_t ChipSelectHighTime;  /* 片选保持高电平的时间 */
  uint32_t ClockMode;           /* SPI 模式 0/3 */
  uint32_t FlashID;             /* 用哪个 Bank */
  uint32_t DualFlash;           /* 是否双闪存 */
} QSPI_InitTypeDef;
```

**里面没有任何"发什么命令、命令码是多少、要几个 dummy 周期"的字段。** 因为同一个 QUADSPI 外设可以挂几十种不同的 Flash 芯片（W25Q64 / MX25L / S25FL / GD25 …），它们的命令码、地址位宽、dummy 周期数各不相同。代码生成器无从得知你焊的是哪一颗，所以这部分**只能按芯片数据手册手写**。

> 也就是说：**`MX_QUADSPI_Init()` 交给 CubeMX 生成，`QSPI_W25Qxx_*()` 手写在 BSP 里** —— 这不是"重复造轮子"，而是这两层的天然分界线。
>
> 本工程的 `BSP/Src/qspi_w25q64.c` 是在 **反客（FANKE）公开的 W25Q64 驱动**基础上整理而来的：把其中属于"外设配置"的 `MX_QUADSPI_Init()` 和 `HAL_QSPI_MspInit()` **删掉**（改由 CubeMX 生成的 `Core/Src/quadspi.c` 承担，参数完全一致），只保留"芯片命令协议"层，并按本工程的工具链做了若干适配。逐条改动见 [5.5](#55-w25qxx-驱动的来源与改动说明)。

## 5.2 `BSP/Inc/led.h` —— LED 控制宏（纯头文件模块）

因为 PC13 的配置已经由 CubeMX 的 `MX_GPIO_Init()` 完成了（见 4.2.4），所以 **LED 这部分不需要 `.c` 文件**，只有一个头文件：

```c
/* <工程根>/BSP/Inc/led.h */
#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/*------------------------------------------ LED控制宏 ----------------------------------*/

#define LED1_ON      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)  /* 输出低电平，点亮LED1 */
#define LED1_OFF     HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET)    /* 输出高电平，关闭LED1 */
#define LED1_Toggle  HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin)                 /* 翻转IO口状态 */

#ifdef __cplusplus
}
#endif

#endif /* __LED_H */
```

要点：

| 要点 | 说明 |
|---|---|
| 依赖 `main.h` 里的 `LED1_Pin` / `LED1_GPIO_Port` | 所以 3.4.1 的 User Label 必须叫 `LED1` |
| 用宏而不是函数 | 零调用开销；且宏的展开体是标准的 HAL 调用，行为可预期 |
| **不要**在这里再写 `LED_Init()` | 那是重复 CubeMX 的 GPIO 配置（本工程早期版本犯过这个错，已删除） |
| 谁要用就 `#include "led.h"` | 本工程的 `main.c` 目前**没有**用它（因为上电点亮 LED 已由 `MX_GPIO_Init()` 完成）；需要时（例如做升级指示）再包含 |

📌 **用法示例**（假设以后要加"引导时闪一下 LED"）：

```c
#include "led.h"
...
LED1_ON;
HAL_Delay(100);
LED1_OFF;
```

> ⚠️ `HAL_Delay()` 依赖 SysTick。而 boot 在跳转前会关掉 SysTick，**所以 `HAL_Delay()` 只能在跳转之前用**。

## 5.3 `BSP/Inc/qspi_w25q64.h` —— 驱动接口与宏定义

```c
/* <工程根>/BSP/Inc/qspi_w25q64.h */
#include "quadspi.h"      /* ← 为了拿到 extern QSPI_HandleTypeDef hqspi; 和 MX_QUADSPI_Init() */
#include <stdio.h>        /* ← 为了 printf（重定向在 Library 层） */
```

> 💡 **注意这里引用的是 CubeMX 生成的 `quadspi.h`**，而不是自己再定义一遍句柄。这是 5.1 那条原则的具体体现：句柄归 CubeMX。

### 5.3.1 返回值宏

```c
#define QSPI_W25Qxx_OK                  0     // W25Qxx通信正常
#define W25Qxx_ERROR_INIT              -1     // 初始化错误
#define W25Qxx_ERROR_WriteEnable       -2     // 写使能错误
#define W25Qxx_ERROR_AUTOPOLLING       -3     // 轮询等待错误，无响应
#define W25Qxx_ERROR_Erase             -4     // 擦除错误
#define W25Qxx_ERROR_TRANSMIT          -5     // 传输错误
#define W25Qxx_ERROR_MemoryMapped      -6     // 内存映射模式错误
```

所有函数返回 `int8_t`，`0` 表示成功、负值表示各种失败。调用方用 `if (xxx() != QSPI_W25Qxx_OK)` 判断即可。

### 5.3.2 命令码宏

```c
#define W25Qxx_CMD_EnableReset          0x66   // 使能复位
#define W25Qxx_CMD_ResetDevice          0x99   // 复位器件
#define W25Qxx_CMD_JedecID              0x9F   // 读 JEDEC ID
#define W25Qxx_CMD_WriteEnable          0x06   // 写使能

#define W25Qxx_CMD_SectorErase          0x20   // 扇区擦除，4K字节
#define W25Qxx_CMD_BlockErase_32K       0x52   // 块擦除，32K字节
#define W25Qxx_CMD_BlockErase_64K       0xD8   // 块擦除，64K字节
#define W25Qxx_CMD_ChipErase            0xC7   // 整片擦除

#define W25Qxx_CMD_QuadInputPageProgram 0x32   // 1-1-4模式页编程
#define W25Qxx_CMD_FastReadQuad_IO      0xEB   // 1-4-4模式快速读取

#define W25Qxx_CMD_ReadStatus_REG1      0x05   // 读状态寄存器1
#define W25Qxx_Status_REG1_BUSY         0x01   // 状态寄存器1 的 BUSY 位（bit0）
#define W25Qxx_Status_REG1_WEL          0x02   // 状态寄存器1 的 WEL  位（bit1）
```

💡 **"1-1-4" / "1-4-4" 是什么意思**：表示"指令用几根线 / 地址用几根线 / 数据用几根线"。

| 记号 | 含义 | 用在哪 |
|---|---|---|
| 1-1-4 | 指令 1 线、地址 1 线、数据 4 线 | 页编程 `0x32` |
| 1-4-4 | 指令 1 线、地址 4 线、数据 4 线 | 快速读取 `0xEB`、`QSPI_W25Qxx_ReadBuffer()` |
| 1-1-1 | 全部单线 | 读 ID、擦除等命令阶段 |

4 线模式比单线快 4 倍，是 QSPI 的意义所在。

### 5.3.3 容量与地址宏

```c
#define W25Qxx_PageSize                 256        // 页大小，256字节
#define W25Qxx_FlashSize                0x800000   // W25Q64大小，8M字节
#define W25Qxx_FLASH_ID                 0xef4017   // W25Q64 JEDEC ID
#define W25Qxx_ChipErase_TIMEOUT_MAX    100000U    // 整片擦除最大等待时间（ms）
#define W25Qxx_Mem_Addr                 0x90000000 // 内存映射模式的地址 ★
```

| 宏 | 关键点 |
|---|---|
| `W25Qxx_PageSize = 256` | W25Q64 的**编程（写）最小单位是 1 页 = 256 字节**。跨页写必须拆成多次（`WriteBuffer()` 就是干这个的）|
| `W25Qxx_FlashSize = 0x800000` | 8MB |
| `W25Qxx_FLASH_ID = 0xEF4017` | 厂商 EF（Winbond）+ 类型 40 + 容量 17（=2^7=128…实际表示 8MB）。`QSPI_W25Qxx_Init()` 用它做自检 |
| `W25Qxx_Mem_Addr = 0x90000000` | ★ **boot 跳转时读的就是这个地址**（第 7 章用）|

> ⚠️ **`W25Qxx_Mem_Addr` 的值必须与 MPU 区域 1 的 `BaseAddress` 一致**（都是 `0x90000000`），也必须与 QUADSPI 的 Bank 1 映射地址一致。改硬件（比如换到 Bank 2）时要三处一起改，见第 11 章。

### 5.3.4 对外函数声明

```c
/* <工程根>/BSP/Inc/qspi_w25q64.h:66-78 */
int8_t   QSPI_W25Qxx_Init(void);                                      // 复位并读取器件ID
int8_t   QSPI_W25Qxx_Reset(void);                                     // 复位器件
uint32_t QSPI_W25Qxx_ReadID(void);                                    // 读取器件ID
int8_t   QSPI_W25Qxx_MemoryMappedMode(void);                          // 进入内存映射模式

int8_t   QSPI_W25Qxx_SectorErase(uint32_t SectorAddress);             // 4K 扇区擦除
int8_t   QSPI_W25Qxx_BlockErase_32K(uint32_t SectorAddress);          // 32K 块擦除
int8_t   QSPI_W25Qxx_BlockErase_64K(uint32_t SectorAddress);          // 64K 块擦除
int8_t   QSPI_W25Qxx_ChipErase(void);                                 // 整片擦除

int8_t   QSPI_W25Qxx_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
int8_t   QSPI_W25Qxx_WriteBuffer(uint8_t* pData, uint32_t WriteAddr, uint32_t Size);
int8_t   QSPI_W25Qxx_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead);
```

**头文件声明 11 个对外函数**；`.c` 里另有 2 个**内部辅助函数**（`QSPI_W25Qxx_AutoPollingMemReady`、`QSPI_W25Qxx_WriteEnable`）没有被声明到头文件里，只在驱动内部使用。

## 5.4 `BSP/Src/qspi_w25q64.c` —— 驱动实现

### 5.4.1 函数清单

✅ **已验证**：文件里共 **13 个函数定义**，行号如下（当前工程状态）：

| 行号 | 函数 | 对外? | 一句话作用 |
|---|---|---|---|
| 40 | `QSPI_W25Qxx_Init` | ✅ | 复位芯片 + 读 ID + 与 `0xEF4017` 比对 |
| 69 | `QSPI_W25Qxx_AutoPollingMemReady` | 内部 | 轮询状态寄存器直到 BUSY=0（"芯片忙不忙"）|
| 111 | `QSPI_W25Qxx_Reset` | ✅ | 发 `0x66`+`0x99` 软复位 |
| 159 | `QSPI_W25Qxx_ReadID` | ✅ | 发 `0x9F` 读 3 字节 JEDEC ID |
| 201 | `QSPI_W25Qxx_MemoryMappedMode` | ✅ | ★ 发 `0xEB` 并切到内存映射模式 |
| 238 | `QSPI_W25Qxx_WriteEnable` | 内部 | 发 `0x06` 并轮询 WEL 位 |
| 299 | `QSPI_W25Qxx_SectorErase` | ✅ | 4K 扇区擦除 |
| 351 | `QSPI_W25Qxx_BlockErase_32K` | ✅ | 32K 块擦除 |
| 404 | `QSPI_W25Qxx_BlockErase_64K` | ✅ | 64K 块擦除 |
| 456 | `QSPI_W25Qxx_ChipErase` | ✅ | 整片擦除（约 20 秒）|
| 528 | `QSPI_W25Qxx_WritePage` | ✅ | 按页（≤256 字节）写入 |
| 592 | `QSPI_W25Qxx_WriteBuffer` | ✅ | 任意长度写入（内部按页拆分）|
| 666 | `QSPI_W25Qxx_ReadBuffer` | ✅ | 任意长度读取（1-4-4 快速读）|

> **bootloader 实际上只用到其中 3 个**：`QSPI_W25Qxx_MemoryMappedMode()`（必需）、以及它内部调用的 `QSPI_W25Qxx_Reset()` 和 `QSPI_W25Qxx_AutoPollingMemReady()`。其余擦除/编程函数是留给"以后做 QSPI 在线升级"用的 —— 保留完整版的好处是以后不用再补，代价是约 10KB 的 Flash。若你追求极致精简，可以只留那 3 个。

### 5.4.2 逐个函数说明

#### （1）`QSPI_W25Qxx_AutoPollingMemReady()` —— 最基础的"等待"机制

**这是整个驱动里最重要的一个函数**，其他所有会改变 Flash 内容的操作（擦除、写入）都要靠它等待完成。

```c
/* <工程根>/BSP/Src/qspi_w25q64.c:69-107（节选） */
int8_t QSPI_W25Qxx_AutoPollingMemReady(void)
{
	QSPI_CommandTypeDef     s_command = {0};
	QSPI_AutoPollingTypeDef s_config  = {0};

	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	s_command.AddressMode       = QSPI_ADDRESS_NONE;      /* 读状态寄存器不需要地址 */
	s_command.DataMode          = QSPI_DATA_1_LINE;
	s_command.DummyCycles       = 0;
	s_command.Instruction       = W25Qxx_CMD_ReadStatus_REG1;   /* 0x05 */

	s_config.Match           = 0;                          /* 期望值：0 */
	s_config.MatchMode       = QSPI_MATCH_MODE_AND;        /* 与运算 */
	s_config.Interval        = 0x10;                       /* 轮询间隔 */
	s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE; /* 匹配后自动停 */
	s_config.StatusBytesSize = 1;
	s_config.Mask            = W25Qxx_Status_REG1_BUSY;    /* 只看 bit0 */

	if (HAL_QSPI_AutoPolling(&hqspi, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;
	}
	return QSPI_W25Qxx_OK;
}
```

💡 **原理：AutoPolling（自动轮询）是 QUADSPI 的硬件功能**

Flash 的擦除/写入需要几十毫秒到几十秒，期间芯片把状态寄存器 1 的 **bit0（BUSY）** 置 1。传统做法是 CPU 循环去读状态寄存器，浪费 CPU 时间。QUADSPI 外设可以自己干这件事：

> "反复发 `0x05` 读状态寄存器，把读到的字节与 `Mask` 相与，结果等于 `Match` 就自动停止。"

CPU 只需要调用一次 `HAL_QSPI_AutoPolling()`，硬件会把这事做完（阻塞等待）。参数含义：

| 参数 | 值 | 含义 |
|---|---|---|
| `Match` | 0 | 期望 `(状态字节 & Mask) == 0` |
| `Mask` | `0x01`（BUSY 位）| 只关心 bit0 |
| `MatchMode` | AND | 用与运算筛选 |
| `AutomaticStop` | ENABLE | 匹配后硬件自动停止轮询 |
| `Interval` | 0x10 | 两次轮询之间的时钟周期数 |

所以这个函数的效果是：**"一直等到 BUSY 位变成 0"**。

#### （2）`QSPI_W25Qxx_WriteEnable()` —— 写操作的前置条件

Flash 有一条安全规定：**任何会改变内容的命令（擦除、页编程）之前，必须先发一次"写使能"命令 `0x06`**，否则命令会被忽略。

```c
/* <工程根>/BSP/Src/qspi_w25q64.c:238-...（节选） */
	s_command.Instruction = W25Qxx_CMD_WriteEnable;    /* 0x06 */
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
		return W25Qxx_ERROR_WriteEnable;

	/* 然后轮询状态寄存器，直到 WEL(bit1) 变成 1 */
	s_config.Match = 0x02;
	s_config.Mask  = W25Qxx_Status_REG1_WEL;
	...
	s_command.Instruction = W25Qxx_CMD_ReadStatus_REG1;   /* 0x05 */
	s_command.DataMode    = QSPI_DATA_1_LINE;
	s_command.NbData      = 1;
	HAL_QSPI_AutoPolling(&hqspi, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE);
```

注意它用 AutoPolling 确认 **WEL（Write Enable Latch，bit1）** 已经置 1 —— 这比"发完命令就当成功"可靠。

> ⚠️ **`0x06` 是一次性的**：每次擦除/编程之前都要重新发。

#### （3）`QSPI_W25Qxx_Reset()` —— 软复位

```c
/* 第一步：发使能复位命令 0x66 */
	s_command.Instruction = W25Qxx_CMD_EnableReset;   /* 0x66 */
	HAL_QSPI_Command(...);
	QSPI_W25Qxx_AutoPollingMemReady();

/* 第二步：发复位命令 0x99 */
	s_command.Instruction = W25Qxx_CMD_ResetDevice;   /* 0x99 */
	HAL_QSPI_Command(...);
	QSPI_W25Qxx_AutoPollingMemReady();
```

W25Q64 规定复位必须**两步**：先 `0x66`（告诉芯片"我要复位了"），再 `0x99`（真正复位）。这是为了防止误触发。

复位后芯片进入已知状态（时钟模式、读写模式都回到默认）。`QSPI_W25Qxx_Init()` 和 `QSPI_W25Qxx_MemoryMappedMode()` 的开头都会调用它。

#### （4）`QSPI_W25Qxx_ReadID()` —— 读 JEDEC ID

```c
	s_command.Instruction = W25Qxx_CMD_JedecID;    /* 0x9F */
	s_command.AddressMode = QSPI_ADDRESS_NONE;     /* 不需要地址 */
	s_command.DataMode    = QSPI_DATA_1_LINE;      /* 单线读 */
	s_command.NbData      = 3;                     /* 3 字节 */

	HAL_QSPI_Command(&hqspi, &s_command, ...);     /* 发命令 */
	HAL_QSPI_Receive(&hqspi, QSPI_ReceiveBuff, ...);/* 收数据 */

	W25Qxx_ID = (QSPI_ReceiveBuff[0] << 16) | (QSPI_ReceiveBuff[1] << 8) | QSPI_ReceiveBuff[2];
```

返回 24 位 ID，W25Q64 应为 `0xEF4017`。

#### （5）`QSPI_W25Qxx_MemoryMappedMode()` —— ★ 本工程的核心

```c
/* <工程根>/BSP/Src/qspi_w25q64.c:201-234（节选） */
	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;      /* 指令 1 线 */
	s_command.AddressSize       = QSPI_ADDRESS_24_BITS;         /* 24 位地址（8MB 需要 23 位）*/
	s_command.AddressMode       = QSPI_ADDRESS_4_LINES;         /* 地址 4 线 */
	s_command.DataMode          = QSPI_DATA_4_LINES;            /* 数据 4 线 */
	s_command.DummyCycles       = 6;                            /* 6 个空周期 */
	s_command.Instruction       = W25Qxx_CMD_FastReadQuad_IO;   /* 0xEB */

	s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;  /* 禁用超时 */
	s_mem_mapped_cfg.TimeOutPeriod     = 0;

	QSPI_W25Qxx_Reset();     /* 先复位，确保芯片状态干净 */

	if (HAL_QSPI_MemoryMapped(&hqspi, &s_command, &s_mem_mapped_cfg) != HAL_OK)
		return W25Qxx_ERROR_MemoryMapped;
	return QSPI_W25Qxx_OK;
```

**逐项解释**：

| 参数 | 值 | 为什么 |
|---|---|---|
| `Instruction = 0xEB` | Fast Read Quad I/O | 这是 W25Q64 支持的最快读取命令（1-4-4 模式）|
| `AddressMode = 4_LINES` | 地址走 4 根线 | `0xEB` 的特性 |
| `DataMode = 4_LINES` | 数据走 4 根线 | 同上 —— 注意这里是**内存映射模式的 DataMode**，与间接模式读写不同 |
| `DummyCycles = 6` | 6 个空周期 | ⚠️ **这是 W25Q64 在 `0xEB` 下的规定值**（数据手册 "Fast Read Quad I/O" 时序）。少了会读到错位数据；换别的芯片型号必须重查手册 |
| `TimeOutActivation = DISABLE` | 关闭超时计数器 | 开启超时会导致 nCS 周期性被拉高、内存映射访问中途被打断。**内存映射模式必须关掉** |
| `QSPI_W25Qxx_Reset()` 放在前面 | | 确保芯片从已知状态进入映射模式 |

**调用这个函数之后**：CPU 读 `0x90000000 + N` 就等于读 Flash 第 N 个字节。这就是 boot 能跳转到 app 的前提。

> ⚠️ **内存映射模式下只能读**。要写 Flash 必须先退出映射模式（调用 `HAL_QSPI_Abort()` 或重新 `HAL_QSPI_Init()`），再用间接模式的擦除/编程函数。本 boot 不做写入，所以不涉及。

#### （6）`QSPI_W25Qxx_SectorErase()` / `BlockErase_32K()` / `BlockErase_64K()` / `ChipErase()`

四个擦除函数结构完全一样，只是命令码和地址模式不同：

| 函数 | 命令 | 地址 | 擦除粒度 | 参考时间 |
|---|---|---|---|---|
| `SectorErase` | `0x20` | 1 线地址 | 4KB | 45ms（最大 400ms）|
| `BlockErase_32K` | `0x52` | 1 线地址 | 32KB | 120ms（最大 1600ms）|
| `BlockErase_64K` | `0xD8` | 1 线地址 | 64KB | 150ms（最大 2000ms）|
| `ChipErase` | `0xC7` | **无地址** | 整片 8MB | 20s（最大 100s）|

统一流程：

```
① WriteEnable()          发 0x06 + 等 WEL=1
② HAL_QSPI_Command()     发擦除命令（带地址）
③ AutoPollingMemReady()  等 BUSY=0（等擦除真正完成）
```

`ChipErase` 的特殊之处：它没有地址（`AddressMode = QSPI_ADDRESS_NONE`），而且等待超时要用 `W25Qxx_ChipErase_TIMEOUT_MAX = 100000U`（100 秒）而不是默认值。

> ⚠️ **Flash 只能把 1 写成 0，不能把 0 写成 1**。所以"写入"之前必须先"擦除"（擦除会把整片/整块变回全 1）。这是所有 Flash 的共性，也是嵌入式应用中非常常见的一条注意事项。

#### （7）`QSPI_W25Qxx_WritePage()` —— 按页写入

```c
	s_command.AddressMode = QSPI_ADDRESS_1_LINE;        /* 地址 1 线 */
	s_command.DataMode    = QSPI_DATA_4_LINES;          /* 数据 4 线 */
	s_command.DummyCycles = 0;                          /* 页编程没有 dummy */
	s_command.NbData      = NumByteToWrite;             /* ≤ 256 */
	s_command.Address     = WriteAddr;
	s_command.Instruction = W25Qxx_CMD_QuadInputPageProgram;  /* 0x32 */
```

| 限制 | 说明 |
|---|---|
| `NumByteToWrite` **最大 256** | 超过会"绕回"到本页开头覆盖前面的数据（硬件行为，不会报错！）|
| **不能跨页** | 一次页编程必须落在同一 256 字节页内 |
| 写前必须擦除 | 见上面的 ⚠️ |

#### （8）`QSPI_W25Qxx_WriteBuffer()` —— 自动分页的写入

解决上面的"不能跨页"问题：把一段任意长度的数据拆成多页依次写入。

```c
/* <工程根>/BSP/Src/qspi_w25q64.c:592-...（节选） */
	/* 计算当前页还剩多少空间 */
	current_size = W25Qxx_PageSize - (WriteAddr % W25Qxx_PageSize);
	if (current_size > Size) current_size = Size;

	do {
		WriteEnable();                          /* 每页都要重新写使能 */
		WritePage(write_data, current_addr, current_size);
		AutoPollingMemReady();                  /* 等这一页写完 */

		current_addr += current_size;
		write_data   += current_size;
		current_size = ((current_addr + W25Qxx_PageSize) > end_addr)
		               ? (end_addr - current_addr) : W25Qxx_PageSize;
	} while (current_addr < end_addr);
```

> ⚠️ 注意 `WriteEnable()` 在**循环体内** —— 每写一页都要重新发 `0x06`（因为写使能是一次性的）。

#### （9）`QSPI_W25Qxx_ReadBuffer()` —— 任意长度读取

与 `MemoryMappedMode` 用的是同一套 1-4-4 参数（`0xEB` + 4 线地址 + 4 线数据 + 6 dummy），区别是它走**间接模式**，把数据读进 RAM 缓冲区：

```c
	s_command.AddressMode = QSPI_ADDRESS_4_LINES;
	s_command.DataMode    = QSPI_DATA_4_LINES;
	s_command.DummyCycles = 6;
	s_command.NbData      = NumByteToRead;
	s_command.Address     = ReadAddr;
	s_command.Instruction = W25Qxx_CMD_FastReadQuad_IO;   /* 0xEB */

	HAL_QSPI_Command(&hqspi, &s_command, ...);
	HAL_QSPI_Receive(&hqspi, pBuffer, ...);
```

> 💡 **两种读取方式怎么选**：要执行代码（app 在 Flash 里）→ 必须内存映射模式；只想把数据搬到 RAM → 用 `ReadBuffer()`。间接模式靠 HAL 轮询寄存器，实测吞吐约 7MB/s；改用 MDMA 搬运可以到 58MB/s。boot 只读两个 32 位值，用哪种都无所谓。

## 5.5 W25Qxx 驱动的来源与改动说明

✅ **已验证**：本工程的 `BSP/Src/qspi_w25q64.c` 以 **反客（FANKE）公开的 W25Q64 驱动**为基础整理而来。相对该原始版本，全部改动如下（没有别的）：

| # | 改动 | 原因 |
|---|---|---|
| 1 | **删除 `MX_QUADSPI_Init()`** | 与 CubeMX 生成的 `Core/Src/quadspi.c` 里的同名函数**符号重定义**；且参数完全一致，CubeMX 版就是等价实现 |
| 2 | **删除 `HAL_QSPI_MspInit()`** | 同上，CubeMX 在 `quadspi.c` 里生成了一份（时钟+6 根引脚）|
| 3 | **删除引脚配置宏**（`QUADSPI_CLK_PIN` 等 6 组） | 引脚现在由 CubeMX 的 `quadspi.c` 配置，这些宏不再被任何代码使用 |
| 4 | `QSPI_W25Qxx_Init()` 里**去掉对 `MX_QUADSPI_Init()` 的调用** | 主程序已经调用过一次（`main.c` 第 110 行），避免重复初始化（重复执行会再次 `HAL_RCCEx_PeriphCLKConfig` + 重新初始化 GPIO，虽无害但多余）|
| 5 | `#include "usart.h"` → `#include <stdio.h>` | printf 改由 `Library/Src/retarget.c` 重定向（[第 6 章](#第-6-章-library-层printf-重定向)），本文件不再需要串口驱动的头文件 |
| 6 | `printf("...%X", Device_ID)` → `printf("...%X", (unsigned int)Device_ID)` | **GCC 下 `uint32_t` 是 `unsigned long`**，不加强制转换会产生 `-Wformat` 告警 |
| 7 | 所有局部 `QSPI_CommandTypeDef` / `QSPI_AutoPollingTypeDef` 加 `= {0}` 清零 | 原代码声明后未清零，未赋值成员的值不确定；清零更安全，也消除了潜在的 `-Wmaybe-uninitialized` 告警 |
| 8 | 中文 printf 文案改为英文 | 源文件是 UTF-8，中文经串口发出后在按 GBK 显示的终端上会乱码 |
| 9 | 文件头补上说明注释 | 便于理解为什么删掉了那两个函数、以及本层的职责边界 |

**没有改动**：所有命令参数（命令码、线宽、dummy 周期、AutoPolling 配置）、函数逻辑、返回值和调用顺序。

## 5.6 W25Q64 命令速查表

| 命令 | 码 | 地址线 | 数据线 | 用途 | 本工程哪个函数用 |
|---|---|---|---|---|---|
| Enable Reset | `0x66` | — | — | 复位使能 | `Reset()` |
| Reset Device | `0x99` | — | — | 执行复位 | `Reset()` |
| JEDEC ID | `0x9F` | — | 1 | 读厂商/型号 | `ReadID()` |
| Write Enable | `0x06` | — | — | 写使能 | `WriteEnable()` |
| Read Status Reg1 | `0x05` | — | 1 | 读 BUSY/WEL | `AutoPollingMemReady()`、`WriteEnable()` |
| Read Data | `0x03` | 1 | 1 | 普通读（未用）| — |
| Fast Read Quad I/O | `0xEB` | **4** | **4** | 高速读（dummy=6）| `MemoryMappedMode()`、`ReadBuffer()` |
| Page Program | `0x02` | 1 | 1 | 普通页编程（未用）| — |
| Quad Input Page Program | `0x32` | 1 | **4** | 四线页编程 | `WritePage()` |
| Sector Erase 4K | `0x20` | 1 | — | 4K 扇区擦除 | `SectorErase()` |
| Block Erase 32K | `0x52` | 1 | — | 32K 块擦除 | `BlockErase_32K()` |
| Block Erase 64K | `0xD8` | 1 | — | 64K 块擦除 | `BlockErase_64K()` |
| Chip Erase | `0xC7` | — | — | 整片擦除 | `ChipErase()` |

---

# 第 6 章 Library 层：printf 重定向

## 6.1 为什么需要这一层

boot 需要通过串口告诉我们"我干了什么"，最方便的方式是用 `printf()`。但 `printf()` 是 C 标准库函数，标准库并不知道"往串口写"这件事 —— 它只知道"往一个叫 stdout 的文件描述符写"。所以中间需要一层"翻译"，把标准库的写请求接到 UART 上。

### 6.1.1 ⚠️ 必须重写 `_write`；网上常见的 `fputc` 写法在 GCC 下**无效**

这是最容易白费时间的地方。网上很多串口重定向例程写的是 `fputc()`：

```c
/* ❌ 网上常见的写法 —— 在 GCC + newlib 下【完全无效】 */
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 100);
    return ch;
}
```

**这段代码能编译通过、链接也不报错，但 `printf` 一个字都不会输出** —— 因为 GCC 用的是 newlib，而 newlib 的 `printf` 根本不调用 `fputc`。

| C 库 | `printf` 最终调用的函数 | 你要重写哪个 |
|---|---|---|
| **newlib / newlib-nano（GCC 默认）** | `_write()` | **`_write`** |
| 使用"字符级输出钩子"的 C 库 | `fputc()` | `fputc` |

**本工程用的是 GCC + newlib-nano（链接选项 `--specs=nano.specs`），所以必须重写 `_write`。** 如果你换用别的工具链或 C 库，请查其文档确认底层钩子是 `_write` 还是 `fputc` —— 判断错了的表现就是"编译一切正常，串口毫无输出"。

**如果你把这句直接搬到 GCC 工程里，`printf` 不会有任何输出**，而且不会报错 —— 因为 `fputc` 确实被编译进去了，只是 `printf` 根本不调用它。这就是为什么本工程专门做了一个 `Library` 层。

### 6.1.2 GCC 下的完整调用链

```
printf("W25Q64 OK,flash ID:%X\r\n", id)
   │
   ▼
vfprintf()                          标准库内部格式化
   │
   ▼
_write_r()                          线程安全包装（newlib 内部）
   │
   ▼
_write(fd, ptr, len)   ◀──────────  ★ 我们重写这个（Library/Src/retarget.c）
   │
   ▼
HAL_UART_Transmit(&huart1, ...)     真正把数据推给串口
```

> 💡 **补充**：还有一条"更底层"的路 —— 有些工程重写的是 `__io_putchar(int ch)`（CubeMX 的 `syscalls.c` 里预留了它的弱定义）。那样需要自己实现带缓冲的 `_write` 循环调用 `__io_putchar`。本工程用的是 `Library` 库直接重写 `_write` 的方案，**两者只能选一个**，不要都写（会重复定义）。

## 6.2 Library 的三个文件

### 6.2.1 `Library/Inc/retarget.h`

```c
/* <工程根>/Library/Inc/retarget.h */
#ifndef _RETARGET_H__
#define _RETARGET_H__

#include "stm32h7xx_hal.h"
#include <sys/stat.h>
#include <stdio.h>

void RetargetInit(UART_HandleTypeDef *huart);

int _isatty(int fd);
int _write(int fd, char *ptr, int len);
int _close(int fd);
int _lseek(int fd, int ptr, int dir);
int _read(int fd, char *ptr, int len);
int _fstat(int fd, struct stat *st);

#endif //_RETARGET_H__
```

**对外只需要一个函数：`RetargetInit()`**，其余 6 个是 newlib 要求的系统调用桩（standard system call stubs）。

### 6.2.2 `Library/Src/retarget.c`

```c
/* <工程根>/Library/Src/retarget.c */
UART_HandleTypeDef *gHuart;                    /* 第 15 行：保存"printf 往哪个串口写" */

void RetargetInit(UART_HandleTypeDef *huart) { /* 第 17 行 */
    gHuart = huart;
    /* Disable I/O buffering for STDOUT stream, so that
     * chars are sent out as soon as they are printed. */
    setvbuf(stdout, NULL, _IONBF, 0);          /* ★ 关闭缓冲，立即输出 */
}

int _write(int fd, char *ptr, int len) {       /* 第 33 行：★ 核心 */
    HAL_StatusTypeDef hstatus;
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        hstatus = HAL_UART_Transmit(gHuart, (uint8_t *) ptr, len, HAL_MAX_DELAY);
        if (hstatus == HAL_OK) return len;
        else return EIO;
    }
    errno = EBADF;
    return -1;
}
```

**两个关键点**：

| 点 | 说明 |
|---|---|
| `setvbuf(stdout, NULL, _IONBF, 0)` | 默认 stdout 是**行缓冲/全缓冲**的，不带换行符的内容可能一直攒着不发出。设为 `_IONBF`（无缓冲）后，每次 `printf` 都立刻发出去 —— 对 boot 调试非常重要 |
| `HAL_UART_Transmit(..., HAL_MAX_DELAY)` | 超时参数是"无限等待"。⚠️ 见下面的注意事项 |

> ⚠️ **`HAL_MAX_DELAY` 的隐患**：`HAL_UART_Transmit()` 内部靠 `HAL_GetTick()` 判断超时，而 `HAL_GetTick()` 依赖 **SysTick 中断**。本工程在跳转前会关掉 SysTick（第 7 章），所以：
> **一旦 SysTick 被关掉，任何再调用 `printf` 的操作都可能永久卡死**（因为超时永远不会到期）。
> 本工程的对策是：把最后一条 `printf` 放在关 SysTick **之前**。如果你以后要在跳转后（或跳转前的临界区里）打印，务必注意这一点。

其余 4 个函数（`_isatty` / `_close` / `_lseek` / `_read` / `_fstat`）都是"假装成功"的桩函数，只有 `_read` 真的实现了从串口收 1 字节（本工程用不到）。

### 6.2.3 `Library/CMakeLists.txt`

```cmake
# <工程根>/Library/CMakeLists.txt
# 查找目录下的所有源文件
file(GLOB SOURCES "Src/*.c")

# 添加一个静态库
add_library(user_library STATIC ${SOURCES})

# 指定头文件目录
target_include_directories(user_library
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

# 链接 STM32CubeMX 生成的目标
target_link_libraries(user_library
        PRIVATE
        stm32cubemx
)
```

| 要点 | 说明 |
|---|---|
| `file(GLOB SOURCES "Src/*.c")` | 自动收集 `Src/` 下所有 `.c`。**新增文件后必须重新运行 CMake 配置**（`file(GLOB)` 不会自动感知新文件）|
| `add_library(user_library STATIC ...)` | 编译成静态库 `libuser_library.a` |
| `PUBLIC` include | 让链接它的可执行文件也能 `#include "retarget.h"` |
| `PRIVATE stm32cubemx` | 只为拿到 HAL 头文件路径和编译宏（`USE_HAL_DRIVER`、`STM32H750xx`）|

### 6.2.4 在 main.c 里怎么用

```c
/* <工程根>/Core/Src/main.c:29 —— 包含头文件 */
#include "retarget.h"			/* Library: printf -> USART1 */

/* <工程根>/Core/Src/main.c:119 —— 初始化（必须在第一次 printf 之前） */
RetargetInit(&huart1);
```

> ⚠️ **顺序很重要**：`RetargetInit()` 必须在**任何 `printf` 之前**调用，否则 `gHuart` 是空指针，`_write` 会往 `NULL` 发数据（HardFault）。
> 同时，`huart1` 本身也必须已经由 `MX_USART1_UART_Init()` 初始化完毕 —— 所以 `RetargetInit()` 要放在 `MX_USART1_UART_Init()` **之后**。

## 6.3 `syscalls.c` 里需要注释掉 4 个函数

### 6.3.1 现象

如果不做处理，链接时会报**重复定义**：

```
multiple definition of `_close'; .../syscalls.c:92: first defined here
multiple definition of `_fstat'; ...
multiple definition of `_isatty'; ...
multiple definition of `_lseek'; ...
```

### 6.3.2 原因：两处都定义了同一批函数

CubeMX 生成的 `Core/Src/syscalls.c` 里有全套 newlib 系统调用桩，而我们的 `Library/Src/retarget.c` 里**也有一套**。两者的交集有 6 个：`_isatty`、`_write`、`_close`、`_lseek`、`_read`、`_fstat`。

**关键区别在于"弱符号"**：

| 函数 | `syscalls.c` 里的声明 | `retarget.c` 里的声明 | 会冲突吗 |
|---|---|---|---|
| `_read` | `__attribute__((weak)) int _read(...)` | `int _read(...)`（强） | ❌ 不冲突，链接器选强符号（retarget 的）|
| `_write` | `__attribute__((weak)) int _write(...)` | `int _write(...)`（强） | ❌ 不冲突，同上 |
| `_isatty` | `int _isatty(...)`（**强**） | `int _isatty(...)`（强） | ✅ **冲突** |
| `_close` | `int _close(...)`（**强**） | `int _close(...)`（强） | ✅ **冲突** |
| `_lseek` | `int _lseek(...)`（**强**） | `int _lseek(...)`（强） | ✅ **冲突** |
| `_fstat` | `int _fstat(...)`（**强**） | `int _fstat(...)`（强） | ✅ **冲突** |

**所以只需要注释掉 4 个**（`_isatty` / `_close` / `_lseek` / `_fstat`），`_read` 和 `_write` 因为 `syscalls.c` 里是弱符号，会被 `retarget.c` 的强符号自动覆盖，不用管。

💡 **原理：弱符号（weak symbol）**

`__attribute__((weak))` 告诉链接器："这个定义优先级低，如果别处有同名的强定义，就用别人的。" 这是 GCC 的扩展。链接规则：

```
强符号 + 强符号  → 报"重复定义"错误
强符号 + 弱符号  → 用强符号，弱符号被丢弃（不报错）
弱符号 + 弱符号  → 任选一个
```

### 6.3.3 怎么改

✅ **已验证**：本工程在 `Core/Src/syscalls.c` 里用 `#if 0 ... #endif` 把这 4 个函数包了起来。

```c
/* <工程根>/Core/Src/syscalls.c:92-126 */
/*--------------------------------------------------------------------------------*/
/* The 4 functions below (_close, _fstat, _isatty, _lseek) are already provided   */
/* by Library/Src/retarget.c, which retargets printf() to the UART.               */
/* Keeping both would cause "multiple definition" link errors, so they are        */
/* commented out here. (_read and _write above are weak, so retarget.c wins.)     */
/*--------------------------------------------------------------------------------*/
#if 0
int _close(int file)
{
  (void)file;
  return -1;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}
#endif /* 0 */
```

> 💡 **为什么用 `#if 0` 而不是 `/* */`**：这几个函数体内部本来就有 `/* */` 注释，用块注释包起来会把注释提前截断（C 注释不能嵌套）。`#if 0` 是预处理层面的"注释掉"，最安全，也容易恢复（改成 `#if 1` 即可）。
>
> ⚠️ `Core/Src/syscalls.c` 是 **CubeMX 生成的文件**。CubeMX 通常不会覆盖它，但如果你重新生成后发现链接又报重复定义，回到这里再包一次即可。

### 6.3.4 验证符号归属

编译后可以用 `nm` 确认这 6 个符号都来自 `retarget.c` 那一份：

```bat
arm-none-eabi-nm build\Debug\STM32H750XBH6_Bootloader.elf | findstr /R "_write$ _read$ _close$ _fstat$ _isatty$ _lseek$"
```

✅ **已验证**：本工程的输出（地址连续，说明来自同一个目标文件 `retarget.o`）：

```
08000f80 T RetargetInit
08000f9c T _isatty
08000fb4 T _write
08000fe8 T _close
08001000 T _lseek
08001010 T _read
08001040 T _fstat
```

再确认没有"未定义符号"：

```bat
arm-none-eabi-nm -u build\Debug\STM32H750XBH6_Bootloader.elf
```

✅ **已验证**：输出为空（本工程没有悬空的未定义符号）。

## 6.4 小结：三个"printf 重定向"方案的对比

| 方案 | 重写什么 | 优点 | 缺点 |
|---|---|---|---|
| **本工程：Library 库** | `_write` 等 6 个 | 完整、标准、可复用；`_isatty` 等桩函数齐全 | 需处理与 `syscalls.c` 的 4 个重复定义 |
| 只用 `__io_putchar` | `__io_putchar` | 只需一个函数 | 依赖 `syscalls.c` 里那个带缓冲的 `_write` 循环，效率略低；`syscalls.c` 被覆盖就失效 |
| 重写 `fputc`（网上常见写法）| `fputc` | 代码短 | **在 GCC + newlib 下完全无效**，`printf` 不会有任何输出 |

---

# 第 7 章 引导流程实现

本章讲 `Core/Src/main.c` 里 `/* USER CODE BEGIN 2 */` 到 `/* USER CODE END 2 */` 之间的那段引导逻辑 —— **这是整个 bootloader 的灵魂**，也是唯一一处"CubeMX 生成不了、必须手写"的主流程。

## 7.1 完整代码

✅ **已验证**：本工程当前的实际内容（`<工程根>/Core/Src/main.c:112-149`）：

```c
  /* USER CODE BEGIN 2 */
  /* 说明：QSPI 内存映射区(0x90000000)的 MPU 访问权限由 CubeMX 生成的
     MPU_Config() 负责（CORTEX_M7 -> MPU Region 1 Settings，
     BaseAddress=0x90000000 / Size=128MB / AccessPermission=FULL_ACCESS），
     它在 main() 开头已经被调用，这里不需要再做任何 MPU 配置。 */

  /* printf()输出到USART1：由 Library/retarget.c 提供重定向 */
  RetargetInit(&huart1);

  printf("\r\n***************************************\r\n");

  if (QSPI_W25Qxx_Init() == QSPI_W25Qxx_OK) /* 复位W25Q64并读取器件ID */
  {
      QSPI_W25Qxx_MemoryMappedMode(); /* 配置QSPI为内存映射模式，之后W25Q64被映射到 0x90000000 */

      printf("Jump to the user application in W25Q64 >>>\r\n\r\n");

      /* 取出用户程序的初始MSP和复位向量（一定要在关Cache/切栈之前读出来） */
      JumpToApplication = (pFunction) (*(__IO uint32_t *) (W25Qxx_Mem_Addr + 4));
      AppStackPointer = *(__IO uint32_t *) W25Qxx_Mem_Addr;

      /* 跳转前把CPU恢复成干净状态：关Cache、停SysTick、关闭MPU（恢复复位默认状态） */
      SCB_DisableICache();
      SCB_DisableDCache();

      SysTick->CTRL = 0; /* 关闭SysTick */
      SysTick->LOAD = 0; /* 清零重载值 */
      SysTick->VAL = 0; /* 清零计数值 */

      HAL_MPU_Disable();

      __set_MSP(AppStackPointer); /* 设置主堆栈指针为用户程序的初始MSP */
      JumpToApplication(); /* 执行跳转 */
  } else {
      printf("W25Q64 not found, boot aborted.\r\n");
  }

  /* USER CODE END 2 */
```

配套的两个全局变量和函数指针类型（在文件上半部分）：

```c
/* <工程根>/Core/Src/main.c:34-37 */
/* USER CODE BEGIN PTD */
typedef void (*pFunction)(void);		/* 用于跳转到用户程序的函数指针类型 */
/* USER CODE END PTD */

/* <工程根>/Core/Src/main.c:49-56 */
/* USER CODE BEGIN PV */
/* 这两个变量必须是全局（不能在栈上）：设置MSP之后栈指针就切到用户程序那边了，
   若函数指针和栈指针存放在栈上，跳转时就会取到已经失效的内存 */
static pFunction JumpToApplication;		/* 用户程序入口地址 */
static uint32_t   AppStackPointer;		/* 用户程序的初始MSP */
/* USER CODE END PV */
```

以及头文件：

```c
/* <工程根>/Core/Src/main.c:26-32 */
/* USER CODE BEGIN Includes */
#include "qspi_w25q64.h"	/* BSP: W25Qxx (QSPI Flash) */
#include "retarget.h"		/* Library: printf -> USART1 */
/* USER CODE END Includes */
```

## 7.2 逐行讲解

### 步骤 1：`RetargetInit(&huart1);`

把串口句柄交给 `Library` 层，让后续 `printf` 能输出。

| 要点 | 说明 |
|---|---|
| 必须在 `MX_USART1_UART_Init()` 之后 | `huart1` 得先初始化好 |
| 必须在任何 `printf` 之前 | 否则 `gHuart` 是空指针 |
| 内部还会 `setvbuf(stdout, NULL, _IONBF, 0)` | 关掉 stdout 缓冲，保证立即可见 |

### 步骤 2：`printf("\r\n***************************************\r\n");`

打印分隔线。串口终端上应该看到（115200-8-N-1）：

```

***************************************
```

> 💡 开头有个 `\r\n`，所以你会先看到空一行 —— 这是有意的，用来把上次复位残留的乱码和新内容分开。

### 步骤 3：`if (QSPI_W25Qxx_Init() == QSPI_W25Qxx_OK)`

```c
/* 内部做的事（见 5.4.2）： */
QSPI_W25Qxx_Reset();                        /* 发 0x66 + 0x99 复位芯片 */
Device_ID = QSPI_W25Qxx_ReadID();           /* 发 0x9F 读 3 字节 */
if (Device_ID == W25Qxx_FLASH_ID)           /* 与 0xEF4017 比对 */
{
    printf("W25Q64 OK,flash ID:%X\r\n", ...);
    return QSPI_W25Qxx_OK;                  /* = 0 */
}
else
{
    printf("W25Q64 ERROR!!!!!  ID:%X\r\n", ...);
    return W25Qxx_ERROR_INIT;               /* = -1 */
}
```

**串口上会看到两种结果之一**：

| 情况 | 串口输出 | 后续行为 |
|---|---|---|
| 正常 | `W25Q64 OK,flash ID:EF4017` | 继续往下跳转 |
| 异常 | `W25Q64 ERROR!!!!!  ID:0`（或 `FFFFFF`） | 走 `else` 分支打印 `W25Q64 not found, boot aborted.`，然后停在 `while(1)` |

> 💡 **本工程在这里加了一道检查**：如果 Flash 通信失败（比如引脚速度配错了），`0x90000000` 读到的会是垃圾数据，硬跳过去可能跑飞到难以定位的状态。加了返回值检查后，问题会以"串口打一行 error 然后停住"这种清晰的方式暴露出来。

### 步骤 4：`QSPI_W25Qxx_MemoryMappedMode();`

**这一步之后，`0x90000000` 才变得可读**。内部做两件事：

1. `QSPI_W25Qxx_Reset()` —— 先把芯片复位到已知状态
2. `HAL_QSPI_MemoryMapped()` —— 用 `0xEB` + 4 线地址 + 4 线数据 + 6 dummy 周期配置内存映射模式

> ⚠️ **这一步之前读 `0x90000000` 会读到什么？** 不会 HardFault（MPU 区域 1 已经放行了），但 QUADSPI 外设还没配置成映射模式，读到的数据是**未定义的**（可能是 0，也可能是总线错误）。所以顺序不能颠倒。

### 步骤 5：`printf("Jump to the user application in W25Q64 >>>\r\n\r\n");`

打印跳转提示。

> 💡 **设计考虑：为什么这句打印放在"关 SysTick 之前"**。`printf` 底层的 `HAL_UART_Transmit(..., HAL_MAX_DELAY)` 依赖 `HAL_GetTick()`，而 `HAL_GetTick()` 依赖 SysTick 中断。如果先关掉 SysTick 再打印，超时判断就永远不成立 —— 正常情况下 TXE 会立刻置位、不至于真的卡住，但这是个隐患。因此本工程把这句打印放在关 SysTick 之前，**输出内容和顺序完全不变**，但更稳妥。

### 步骤 6：读出 app 的入口地址和栈指针

```c
JumpToApplication = (pFunction) (*(__IO uint32_t *) (W25Qxx_Mem_Addr + 4));  /* 0x90000004 */
AppStackPointer   = *(__IO uint32_t *) W25Qxx_Mem_Addr;                        /* 0x90000000 */
```

这是**两次对 QSPI Flash 的读取**：

| 地址 | 内容 | 含义 |
|---|---|---|
| `0x90000000` | app 的初始 MSP | app 运行时栈顶应该在哪 |
| `0x90000004` | app 的 Reset_Handler 地址 | app 从哪里开始执行 |

> ⚠️ **注释里那句"一定要在关 Cache/切栈之前读出来"很关键**，原因见 7.4 和 7.5。

#### `(__IO uint32_t *)` 是什么

```c
*(__IO uint32_t *) (W25Qxx_Mem_Addr + 4)
```

拆开看：

| 部分 | 作用 |
|---|---|
| `W25Qxx_Mem_Addr + 4` | = `0x90000004`，一个整数 |
| `(__IO uint32_t *)` | 把它**强制转换成"指向 volatile uint32_t 的指针"** |
| 前面的 `*` | 解引用，读出这个地址上的 4 字节 |

`__IO` 是 CMSIS 定义的宏（`#define __IO volatile`），作用是**告诉编译器"这个地址的内容可能随时变化，不许优化掉这次读取"**。对硬件寄存器访问这是必须的；这里读的是 QSPI 映射区，加上 `volatile` 是好习惯（防止编译器把两次读取合并或重排）。

### 步骤 7：`SCB_DisableICache(); SCB_DisableDCache();`

关闭指令缓存和数据缓存。详见 7.5。

### 步骤 8：停掉 SysTick

```c
SysTick->CTRL = 0;   /* 关掉计数器使能 + 中断使能 */
SysTick->LOAD = 0;   /* 重载值清零 */
SysTick->VAL  = 0;   /* 当前计数值清零 */
```

三个寄存器含义：

| 寄存器 | 位域 | 清零效果 |
|---|---|---|
| `CTRL` | bit0 ENABLE / bit1 TICKINT / bit2 CLKSOURCE | 计数停止、中断关闭 |
| `LOAD` | 重载值（24 位） | 不再产生周期性中断 |
| `VAL` | 当前计数值 | 清掉残留计数 |

> 💡 **为什么要停 SysTick**：目的是"让 app 看到一个干净的 CPU 状态"。如果不停，SysTick 中断会在跳转后继续发生，而那时 CPU 执行的已经是 app 的代码 —— 让一个来路不明的心跳继续跑没有意义。所以**停掉最安全**（关于跳转后中断向量表的归属，见 [7.7](#77-app-侧必须满足的条件) 的说明）。

### 步骤 9：`HAL_MPU_Disable();`

关闭 MPU。详见 7.6。

### 步骤 10：`__set_MSP(AppStackPointer);`

**把 CPU 的栈指针换成 app 的栈顶。**

`__set_MSP()` 是 CMSIS 提供的内联函数，展开后就是一条汇编指令：

```c
__STATIC_FORCEINLINE void __set_MSP(uint32_t topOfMainStack)
{
  __ASM volatile ("MSR msp, %0" : : "r" (topOfMainStack) : );
}
```

> ⚠️ **执行完这一条指令，栈就"搬家"了**。此后所有使用栈的操作（函数调用、局部变量）都会落在 **app 的栈**上，而不再是 boot 的栈。这就是 7.4 那个陷阱的根源。

### 步骤 11：`JumpToApplication();`

**跳转。** 反汇编出来是：

```asm
  ldr  r3, [r3, #0]     ; 从全局变量 JumpToApplication 取值
  blx  r3               ; 带链接跳转（实际上不会返回）
```

`blx` 会顺带把返回地址写进 LR，但因为永远不会返回，所以无所谓。

**执行 `blx` 的那一刻，CPU 从 `0x90000000` 开始的 app 代码取指令 —— boot 的使命结束。**

## 7.3 跳转的三个必备要素

要把 CPU "交班"给另一个程序，必须满足 3 个条件，缺一个都不行：

| # | 要素 | 谁负责 | 本工程怎么做的 |
|---|---|---|---|
| 1 | **栈指针** 指向 app 的栈顶 | boot | `__set_MSP(AppStackPointer)` |
| 2 | **程序计数器** 跳到 app 的入口 | boot | `JumpToApplication()` |
| 3 | **向量表** 指向 app 的向量表 | **需要 app 配合**（见 7.7）| 本工程用的 app 会把向量表放在 `0x90000000`，与 `VTOR` 复位默认值（0）配合的是…（见 7.7 详解）|

💡 **为什么"栈指针"也要换**：每个程序都有自己独立的栈空间。如果沿用 boot 的栈，app 的栈会从 boot 的栈位置开始生长，可能踩坏 boot 的全局变量或越界。所以**交班时必须连栈一起交**。

## 7.4 ⚠️★ 陷阱：为什么 MSP 和入口地址必须放在**全局变量**里

这一段解释一个"看起来只是风格问题、实际上是致命 bug"的细节。

### 7.4.1 错误的写法（直觉上更自然的写法）

初学者很可能会这样写：

```c
/* ❌ 错误示范 */
__set_MSP(*(__IO uint32_t *) W25Qxx_Mem_Addr);
((pFunction)(*(__IO uint32_t *)(W25Qxx_Mem_Addr + 4)))();
```

或者这样：

```c
/* ❌ 也是错的 */
uint32_t msp = *(__IO uint32_t *) W25Qxx_Mem_Addr;      /* 局部变量，在栈上 */
pFunction entry = (pFunction)(*(__IO uint32_t *)(W25Qxx_Mem_Addr + 4));

__set_MSP(msp);      /* ← 执行完这句，栈就换到 app 那边了 */
entry();             /* ← 从"旧栈"上取 entry，取到的是垃圾！ */
```

### 7.4.2 为什么会错

关键在于**局部变量存在栈上**。看第二段代码的执行过程：

```
① uint32_t msp = ...      → msp 这个变量被写进【boot 的栈】
② pFunction entry = ...   → entry 这个变量也被写进【boot 的栈】
③ __set_MSP(msp)          → MSR MSP, rX
                             ↑ 此刻开始，栈指针指向【app 的栈】
④ entry()                 → 编译器要生成：从栈上取 entry 的值 → BLX
                             但此刻"栈上"已经是 app 的栈了！
                             boot 栈里那个 entry 值，CPU 已经找不到了
```

结果：`blx` 跳到一个**完全随机的地址**，程序跑飞。

**这个 bug 的隐蔽性在于**：
- 在 `-O0`（Debug）下**几乎必然触发**（局部变量一定在栈上）
- 在 `-Os`（Release）下**可能恰好不触发**（编译器把变量放进寄存器，不占栈）—— 于是出现"Release 能跑、Debug 就死"这种最让人抓狂的现象
- 反汇编看起来也"没毛病"，只有理解栈切换的人才能发现

### 7.4.3 正确的写法（本工程的做法）

**把这两个值放进全局变量**（全局变量在 `.data`/`.bss` 段，地址固定，与栈无关）：

```c
/* <工程根>/Core/Src/main.c:52-55 */
static pFunction JumpToApplication;		/* 用户程序入口地址 */
static uint32_t   AppStackPointer;		/* 用户程序的初始MSP */

...
    JumpToApplication = (pFunction) (*(__IO uint32_t *) (W25Qxx_Mem_Addr + 4));
    AppStackPointer = *(__IO uint32_t *) W25Qxx_Mem_Addr;
...
    __set_MSP(AppStackPointer);     /* 从全局变量读 → 与栈无关 ✓ */
    JumpToApplication();            /* 从全局变量读 → 与栈无关 ✓ */
```

### 7.4.4 反汇编证据

✅ **已验证**：本工程 Debug 构建下 `main()` 尾部的实际反汇编（地址为示例，你的构建可能不同）：

```asm
; ---- 读 app 的向量表 ----
 80007c2:  ldr   r3, [r3, #0]          ; 读 0x90000004（app 入口地址）
 80007c8:  str   r2, [r3, #0]          ; 存进全局变量 JumpToApplication
 80007ce:  ldr   r3, [r3, #0]          ; 读 0x90000000（app 初始 MSP）
 ...（后面存进全局变量 AppStackPointer）

; ---- 关 Cache ----
 80007d4:  dsb   sy
 80007da:  isb   sy
 ...
; ---- 停 SysTick ----
 800088e:  str   r2, [r3, #0]          ; SysTick->CTRL = 0
 8000894:  str   r2, [r3, #4]          ; SysTick->LOAD = 0
 800089a:  str   r2, [r3, #8]          ; SysTick->VAL  = 0
; ---- 关 MPU ----
 800089c:  bl    8001690 <HAL_MPU_Disable>
; ---- 切栈 + 跳转 ----
 80008a2:  ldr   r3, [r3, #0]          ; ← 从【全局变量】读 AppStackPointer
 80008a8:  msr   MSP, r3               ; ★ 栈在这里搬家
 80008b0:  ldr   r3, [r3, #0]          ; ← 从【全局变量】读 JumpToApplication
 80008b2:  blx   r3                    ; ★ 跳转
```

**看关键点**：`msr MSP, r3` 之后的 `ldr r3, [r3, #0]` 依然是从**固定的全局变量地址**取数，与 MSP 无关 —— 这就是正确的写法。

**核对要点**：`msr MSP` 之后不应出现 `ldr r3, [sp, #xx]` 这类**从栈上取数**的指令 —— 出现了就说明中间值落在了栈上，应改为全局变量。

> 💡 **另一种常见写法（通常也能跑，但更依赖优化行为）**：把 MSP 的值直接写在 `__set_MSP()` 的实参位置上，中间不落地到栈：
> ```c
> JumpToApplication = (pFunction) (*(__IO uint32_t*) (W25Qxx_Mem_Addr + 4));  /* 全局变量 */
> __set_MSP(*(__IO uint32_t*) W25Qxx_Mem_Addr);   /* 内联展开，不经过栈 */
> JumpToApplication();
> ```
> 这种写法能工作的前提是：**函数指针是全局的**，且 **MSP 的值直接在实参位置求值**（编译成"先 load 到寄存器、再 MSR"，中间不落地到栈）。
> 但它比本工程的写法**更容易受优化等级影响**：一旦编译器决定把中间值溢出到栈上，`-O0` 下就会暴露出 7.4.1 那个问题。**本工程把两个值都放进全局变量，任何优化等级下都安全，意图也更清晰。**

## 7.5 为什么跳转前要关 ICache / DCache

### 7.5.1 这两句做了什么

```c
SCB_DisableICache();
SCB_DisableDCache();
```

| 函数 | 实际动作 |
|---|---|
| `SCB_DisableICache()` | 关闭 `SCB->CCR` 的 IC 位，并做 `DSB`/`ISB` 同步 |
| `SCB_DisableDCache()` | **先做 clean + invalidate（按 set/way 遍历整个 D-Cache）**，再关闭 DC 位 |

> 💡 CMSIS 的 `SCB_DisableDCache()` 实现（`Drivers/CMSIS/Include/cachel1_armv7.h`）会读取 `CCSIDR` 得到 cache 的 set/way 数量，然后循环写 `DCCSW` 寄存器把每一行 clean+invalidate 掉，最后才清 `CCR.DC`。所以**调用它不会有脏数据残留**。

### 7.5.2 为什么必须关

| 原因 | 说明 |
|---|---|
| **app 不知道自己继承了缓存** | 缓存是 CPU 内部的，不带"归属"信息。如果 boot 开着 D-Cache 跳过去，app 第一次读某块内存时可能命中 boot 留下的陈旧缓存行，**读到错误数据** |
| **缓存内容与 app 无关** | boot 读过的 QSPI 数据、串口输出缓冲等都留在 D-Cache 里，对 app 而言全是脏数据 |
| **与"复位态"对齐** | CPU 复位时缓存是关闭的。关掉之后 app 面对的环境和它"自己从头启动"时一致 |

> ⚠️ **顺序要求**：建议在**读完 app 的向量表之后**再关缓存。因为那两次读取（`0x90000000` / `0x90000004`）本身也走缓存；先关再读同样正确（只是慢一点），但"先读、后关"的意图更清楚。

### 7.5.3 MPU 里 QSPI 区被标为 CACHEABLE，和这里矛盾吗

不矛盾。两者管的是不同阶段：

| 阶段 | QSPI 区的缓存行为 |
|---|---|
| boot 运行期间（MPU 区域 1 = CACHEABLE） | 读 `0x90000000` 时可以被缓存 —— 没问题，只读两次 |
| 跳转之后（缓存已关闭 / MPU 已关闭） | 走 ARMv7-M 默认内存映射：`0x80000000`~`0x9FFFFFFF` = Normal, **Write-Through** —— 与本工程给区域 1 设的属性（TEX=0, C=1, B=0）**完全一致** |

也就是说：**关不关 MPU，QSPI 这块区域的缓存属性其实是一样的**（见 7.6）。

## 7.6 为什么跳转前要关 MPU

### 7.6.1 这一句做了什么

```c
HAL_MPU_Disable();
```

HAL 的实现（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c`）：

```c
void HAL_MPU_Disable(void)
{
  __DMB();                                    /* 等之前的存储器访问落地 */
  SCB->SHCSR &= ~SCB_SHCSR_MEMFAULTENA_Msk;   /* 关闭 MemManage 异常 */
  MPU->CTRL = 0;                              /* 关闭 MPU，控制寄存器清零 */
}
```

| 动作 | 效果 |
|---|---|
| `MPU->CTRL = 0` | `ENABLE=0` → MPU **完全不参与地址检查**，所有访问走 ARMv7-M 默认内存映射 |
| `SHCSR.MEMFAULTENA = 0` | MemManage 异常被关闭；万一发生 MPU 违规会**升级为 HardFault**（但 MPU 已关，不会有违规）|

对比 `MPU_Config()` 末尾的 `HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT)`，它会把 `MPU->CTRL` 设成 `0x05`（`ENABLE=1, PRIVDEFENA=1`）。

### 7.6.2 关与不关，app 看到的世界有何不同

**关之前**（MPU 生效，区域 0 + 区域 1）：

| 地址范围 | 属性 |
|---|---|
| `0x0000_0000`~`0x1FFF_FFFF`（内部 Flash 等） | 区域 0 的 SR0 被排除 → 默认映射，正常 |
| `0x2000_0000`~`0x3FFF_FFFF`（SRAM） | SR1 被排除 → 默认映射，正常 |
| `0x4000_0000`~`0x5FFF_FFFF`（外设） | SR2 被排除 → 默认映射，正常 |
| **`0x6000_0000`~`0xDFFF_FFFF`**（外部存储器） | **区域 0 的 SR3~SR6 → NO_ACCESS，任何访问都触发 fault** |
| `0x9000_0000`~`0x97FF_FFFF` | 区域 1 覆盖 → FULL_ACCESS、可执行、Normal WT |
| `0xE000_0000` 以上 | SR7 被排除 → 默认映射，正常 |

**关之后**：全部走默认内存映射（详见 [附录 C](#附录-c-armv7-m-默认内存映射表)）

### 7.6.3 关键结论：对 QSPI 区而言，关不关没区别

| | 区域 1 的 MPU 属性 | 默认内存映射对 `0x80000000`~`0x9FFFFFFF` 的属性 |
|---|---|---|
| 内存类型 | Normal | Normal |
| 缓存 | TEX=0, C=1, B=0 → **Write-Through** | **Write-Through** |
| 可执行 | `DisableExec = ENABLE` | **可执行**（该区间不是 XN）|
| 共享 | 非共享 | 非共享 |

**完全一致。** 所以关掉 MPU 不影响 app 从 `0x90000000` 取指令执行。

**唯一消失的东西**是区域 0 对 `0x6000_0000`~`0xDFFF_FFFF` 的"禁止访问"保护。

### 7.6.4 那到底该关还是不该关

这是一个**交接状态的选择题**，两种做法都能工作：

| | 保留这行（关 MPU）—— 本工程选用 | 删掉这行（保持 MPU 开启）|
|---|---|---|
| 与冷启动复位后的状态 | **相同**（复位时 `MPU->CTRL = 0`）| 不同 |
| 与"从不配置 MPU 的引导程序"引导后 | **相同** | 不同 |
| app 在 `main()` 之前访问 SDRAM(`0xC0000000`)/FMC(`0x60000000`) | 正常 | **MemManage → HardFault** |
| app 访问 QSPI(`0x90000000`) | 正常（默认映射放行）| 正常（区域 1 放行）|
| app 需要自己做的事 | 自己配 MPU 也行、不配也行 | **必须先重配 MPU** 才能用外部存储器 |
| ST 官方 `ExtMem_Boot` 的做法 | — | ✅ ST 选择**保持开启** |

**本工程选择"关掉"**，理由：让 app 面对一个与"刚上电"尽可能一致的运行环境，app 不需要"知道 boot 帮它留了什么"，出错面最小。

> 💡 **如果你要删掉这一行**：前提是下载到 W25Q64 里的 app 自己会配置 MPU（比如它也是 CubeMX 生成的、`main()` 开头有 `MPU_Config()`）。删掉后行为就与 ST 官方例程一致了。

### 7.6.5 ⚠️ 两个容易忽略的细节

**① `MPU->CTRL = 0` 并不会清除区域寄存器**

区域 0、区域 1 的 `RBAR`/`RASR` 值**仍然留在寄存器里**，只是不生效。如果 app 里只写了 `HAL_MPU_Enable(...)` 而没有重新编程区域，boot 留下的这套配置会**立刻"复活"**（包括那个禁访的区域 0）。

CubeMX 生成的 app 一定会在自己的 `MPU_Config()` 里先 `HAL_MPU_Disable()` 再逐区域配置，所以实践中不会有问题 —— 但严格说，这是"停用"而不是"复位"。

**② 它顺手把 MemManage 异常也关了**

如果你以后想在 app 里用 `MemManage_Handler` 抓内存越界问题，要记得此刻它是关闭状态，需要在 app 里重新 `HAL_MPU_Enable()` 打开。

## 7.7 app 侧必须满足的条件

boot 只负责"跳过去"，能不能跑起来还取决于 app 自己。一个能被本 boot 正确启动的 app 必须满足：

| # | 条件 | 说明 | 不满足会怎样 |
|---|---|---|---|
| 1 | **链接到 `0x90000000`** | app 的链接脚本里 Flash 起始地址必须是 `0x90000000` | boot 跳过去后取到的是错误的指令地址 |
| 2 | **向量表放在 Flash 最前面** | 即 `0x90000000` = 初始 MSP，`0x90000004` = Reset_Handler | boot 读到的"入口地址"是垃圾 |
| 3 | **初始 MSP 指向有效 RAM** | 通常是 DTCM(`0x20000000`) 或 AXI SRAM(`0x24000000`) 的末尾 | `__set_MSP` 之后立刻 HardFault |
| 4 | **自己处理 MPU / 缓存** | 若 app 要用 SDRAM 等外部存储器，需自己配 MPU（本 boot 在跳转前已关 MPU，所以 app 至少要保证自己开启 MPU 时把 QSPI 区也放行）| 见 7.6 |
| 5 | **不要假设 boot 留下的外设状态** | boot 初始化了 QUADSPI / USART1 / GPIO；app 如果也要用，应自己重新初始化一遍 | 一般没事（HAL 初始化是幂等的）|
| 6 | **中断向量表偏移（VTOR）** | 若 app 的向量表不在 `0x00000000` 也别在 `0x90000000`，需要在 app 的 `SystemInit()` 里设 `SCB->VTOR` | 中断发生时跳到错误的中断处理函数 |

> 💡 **关于 VTOR**：Cortex-M7 复位后 `VTOR = 0x00000000`。有些 STM32H7 型号会把 `0x00000000` 别名到内部 Flash（`0x08000000`）。**本 boot 没有修改 VTOR**，所以跳转后 VTOR 仍是复位值 —— app 必须让自己的向量表处在 VTOR 指向的位置，或者自己在 `main()` 之前把 VTOR 设成 `0x90000000`。这是 app 的责任，不是 boot 的。

---

# 第 8 章 CMake 构建系统

## 8.1 四个构建脚本的分工

工程里有 4 个 `CMakeLists.txt`，职责不同，**改错地方会白忙一场**：

| 文件 | 谁生成 | 会被覆盖吗 | 你要改吗 |
|---|---|---|---|
| `<工程根>/CMakeLists.txt` | CubeMX | ❌ 只生成一次 | ✅ **要改**（加 BSP/Library）|
| `<工程根>/cmake/stm32cubemx/CMakeLists.txt` | CubeMX | ✅ **每次生成都重写** | ❌ **绝对不要改** |
| `<工程根>/BSP/CMakeLists.txt` | 手写 | ❌ | ✅ 你自己维护 |
| `<工程根>/Library/CMakeLists.txt` | 手写 | ❌ | ✅ 你自己维护 |

> ⚠️ **最重要的一条**：`cmake/stm32cubemx/CMakeLists.txt` 是 CubeMX 的"地盘"，你手改的内容会在下次 `GENERATE CODE` 时被抹掉。而顶层 `CMakeLists.txt` 头部明确写着：
> ```
> # This file is generated only once,
> # and is not re-generated if converter is called multiple times.
> # User is free to modify the file as much as necessary
> ```
> 所以**所有用户自定义的构建配置都写进顶层 `CMakeLists.txt`**。

## 8.2 顶层 `CMakeLists.txt` 逐行

```cmake
# <工程根>/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)          # ① 最低 CMake 版本

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)                      # ② C11（CubeMX 生成的代码要求 ≥ C11）
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)                    # 允许 GNU 扩展（__attribute__ 等）

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")             # ③ 默认 Debug
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32H750XBH6_Bootloader)   # ④ 决定输出文件名

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)       # ⑤ 生成 compile_commands.json，给 IDE 索引代码用

# Core project settings
project(${CMAKE_PROJECT_NAME})                # ⑥ ★ 这一句会触发工具链文件加载和编译器探测
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)                        # ⑦ 启用汇编（启动文件是 .s）

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})         # ⑧ 创建可执行目标（先建空的，后面填源文件）

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)           # ⑨ CubeMX 生成的源文件 + HAL 驱动

# Add BSP (LED + W25Qxx / QSPI flash driver)
add_subdirectory(BSP)                         # ⑩ ★ 手写：BSP 静态库

# Add user library (newlib retarget: printf -> UART)
add_subdirectory(Library)                     # ⑪ ★ 手写：retarget 静态库

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)   # ⑫ 去掉一个不存在的隐式库依赖

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx                               # ⑬ CubeMX 的 INTERFACE 库（提供 include 路径和宏）
    bsp                                       # ⑭ ★ 手写
    user_library                              # ⑮ ★ 手写

    # Add user defined libraries
)
```

**相对 CubeMX 原始输出，本工程只改了 3 处**（⑩⑪⑭⑮）：

```diff
  # Add STM32CubeMX generated sources
  add_subdirectory(cmake/stm32cubemx)
+
+ # Add BSP (LED + W25Qxx / QSPI flash driver)
+ add_subdirectory(BSP)
+
+ # Add user library (newlib retarget: printf -> UART)
+ add_subdirectory(Library)
@@
  target_link_libraries(${CMAKE_PROJECT_NAME}
      stm32cubemx
+     bsp
+     user_library
```

### 8.2.1 ⚠️ 为什么顺序不能乱

```
add_subdirectory(cmake/stm32cubemx)   ← 必须先！它定义了 stm32cubemx 和 STM32_Drivers 两个目标
add_subdirectory(BSP)                 ← BSP 的 CMakeLists 里要 target_link_libraries(... stm32cubemx)
add_subdirectory(Library)             ← 同上
```

如果先 `add_subdirectory(BSP)`，会报：

```
CMake Error: The link interface of target "bsp" contains: stm32cubemx
but the target was not found.
```

## 8.3 `cmake/stm32cubemx/CMakeLists.txt`（只读，不要改）

这个文件由 CubeMX 生成，核心内容：

```cmake
# ① 编译宏：告诉 HAL 库用的是哪个芯片、什么供电方式
set(MX_Defines_Syms
	USE_PWR_LDO_SUPPLY     # ← 供电用 LDO（对应 3.3.2 的 Supply Source）
	USE_HAL_DRIVER
	STM32H750xx            # ← 芯片型号
    $<$<CONFIG:Debug>:DEBUG>
)

# ② include 路径
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# ③ 应用源文件（见 4.2.7）
set(MX_Application_Src ... main.c gpio.c quadspi.c usart.c ... startup_stm32h750xx.s)

# ④ HAL 驱动源文件（见 4.2.7）
set(STM32_Drivers_Src ... stm32h7xx_hal_qspi.c stm32h7xx_hal_uart.c ...)

# ⑤ INTERFACE 库：只有 include 路径和宏，没有源文件
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# ⑥ OBJECT 库：HAL 驱动编译成 .o，直接并入最终可执行文件
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)

# ⑦ 把应用源文件挂到可执行目标上
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# ⑧ 链接
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})   # STM32_Drivers + m
```

💡 **`INTERFACE` 和 `OBJECT` 库的区别**（理解了才能看懂依赖图）：

| 类型 | 有编译产物吗 | 作用 |
|---|---|---|
| `INTERFACE` | ❌ 无 | 只传递"使用要求"（include 路径、编译宏）—— 相当于"配置包" |
| `OBJECT` | ✅ 有 `.o` | 编译出目标文件，但**不打包成 .a**，而是把 `.o` 直接列进最终链接命令 |
| `STATIC` | ✅ 有 `.a` | 编译并打包成静态库 `libxxx.a` |

## 8.4 `BSP/CMakeLists.txt`

```cmake
# <工程根>/BSP/CMakeLists.txt
# BSP（板级支持包）：W25Qxx(QSPI Flash) 驱动 + LED 语义层
#   注意：LED 的引脚配置由 CubeMX 生成的 Core/Src/gpio.c 负责，
#         BSP 里只有 BSP/Inc/led.h 这个纯宏头文件，没有 led.c

# 源文件
set(BSP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Src/qspi_w25q64.c"
)

# 添加一个静态库
add_library(bsp STATIC ${BSP_SOURCES})

# 指定头文件目录
target_include_directories(bsp
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

# 链接 STM32CubeMX 生成的目标（提供 HAL 头文件路径、Core/Inc 以及编译宏）
target_link_libraries(bsp
        PUBLIC
        stm32cubemx
)
```

| 要点 | 说明 |
|---|---|
| 显式列出 `Src/qspi_w25q64.c` | 没用 `file(GLOB)`。好处：新增文件时**必须手写**，不会出现"文件加了但没编译"的困惑 |
| `PUBLIC` include | 让链它的可执行文件也能 `#include "qspi_w25q64.h"` |
| `PUBLIC stm32cubemx` | 把 HAL 头文件路径也**传递**给上层（`main.c` 要用 `hqspi` / `huart1`）|
| 没有 `led.c` | LED 只有宏，见 5.2 |

## 8.5 `Library/CMakeLists.txt`

见 [6.2.3](#623-librarycmakeliststxt)，此处不重复。

## 8.6 `cmake/gcc-arm-none-eabi.cmake` —— 工具链文件

这个文件定义了"用什么编译器、加什么编译选项"，由 CubeMX 生成。**关键内容**：

```cmake
# <工程根>/cmake/gcc-arm-none-eabi.cmake
set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
# arm-none-eabi- must be part of path environment      ← ★ 明确指出依赖 PATH
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)     # ← 裸名字，靠 PATH 解析
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)   # ← 交叉编译必须，否则编译器探测会试着链接可执行文件而失败

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard ")   # ← Cortex-M7 + 双精度浮点 + 硬浮点 ABI

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")
#   -Wall              : 打开常规告警（★ 本工程就是靠它发现了 %X 的问题）
#   -fdata-sections /
#   -ffunction-sections: 每个函数/变量独立成段，配合 --gc-sections 剔除未用代码
#   -fstack-usage      : 生成 .su 文件，报告每个函数的栈用量

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")            # Debug: 不优化 + 最全调试信息
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")          # Release: 优化体积 + 无调试信息

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32H750xx_FLASH.ld\"")  # ★ 指定链接脚本
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")   # 用 newlib-nano（体积小）
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
#   -Wl,-Map=...        : 生成 map 文件（排查"代码为什么这么大"很有用）
#   -Wl,--gc-sections   : 丢弃未被引用的段
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")  # 链接后打印内存占用
set(TOOLCHAIN_LINK_LIBRARIES "m")             # 链接 libm（数学库）
```

**几个值得记住的点**：

| 选项 | 为什么需要 |
|---|---|
| `-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard` | H750 是 Cortex-M7，带双精度 FPU。**这三个选项必须和芯片匹配**，否则会 hardfault 或性能暴跌 |
| `--specs=nano.specs` | newlib-nano，比完整 newlib 小很多。代价：`printf` 默认不支持 `%f` 浮点格式（需要另外加链接选项 `-u _printf_float`）|
| `-Wl,--print-memory-usage` | 每次链接都会打印 FLASH/RAM 占用，非常好用 |
| `set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)` | 交叉编译时，CMake 探测编译器会尝试**链接并运行**一个程序；目标平台代码在 PC 上跑不了，所以改成"只编译成静态库"来探测 |

> ⚠️ **不要在这个文件里写死编译器的绝对路径**。本文件用裸名字 `arm-none-eabi-gcc` 是**有意为之**——只要目标电脑装了 STM32CubeCLT 并把它的 `GNU-tools-for-STM32\bin` 加进了 PATH，工程就能直接构建。这也是"工程可移植到别的电脑"的关键（见 2.3 和 2.4）。

## 8.7 目标依赖图

✅ **已验证**：CMake 配置后生成的依赖关系（用 `cmake --build` 的构建图核实）：

```
                    ┌─────────────────────────────────────┐
                    │  stm32cubemx  (INTERFACE)           │
                    │  ├─ include: Core/Inc, HAL, CMSIS   │
                    │  └─ define : USE_HAL_DRIVER,        │
                    │              STM32H750xx,           │
                    │              USE_PWR_LDO_SUPPLY     │
                    └──────────┬───────────┬──────────────┘
                               │           │
              ┌────────────────┘           └───────────────┐
              ▼                                            ▼
   ┌──────────────────────┐                    ┌──────────────────────┐
   │ STM32_Drivers (OBJ)  │                    │  bsp (STATIC)        │
   │ = 所有 HAL 驱动 .c   │                    │  = qspi_w25q64.c     │
   │                      │                    │  → libbsp.a          │
   └──────────┬───────────┘                    └──────────┬───────────┘
              │                                           │
              │            ┌──────────────────────┐       │
              │            │ user_library (STATIC)│       │
              │            │ = retarget.c         │       │
              │            │ → libuser_library.a  │       │
              │            └──────────┬───────────┘       │
              │                       │                   │
              ▼                       ▼                   ▼
   ┌────────────────────────────────────────────────────────────────┐
   │              STM32H750XBH6_Bootloader  (可执行)                 │
   │  源文件：Core/Src/*.c + startup_stm32h750xx.s                   │
   │  链接：  -lm  BSP/libbsp.a  Library/libuser_library.a           │
   │  产物：  .elf / .map / .bin / .hex
   └────────────────────────────────────────────────────────────────┘
```

✅ **已验证**：实际生成的链接命令末尾是：

```
... @CMakeFiles/STM32H750XBH6_Bootloader.dir/objects1.rsp \
    -o STM32H750XBH6_Bootloader.elf  -lm BSP/libbsp.a Library/libuser_library.a
```

`objects1.rsp` 里包含了：`Core/Src/*.o`、`startup_stm32h750xx.s.o`、以及 `STM32_Drivers` 的**全部 HAL 驱动 .o**（因为是 OBJECT 库，直接列出目标文件，不打包成 .a）。

> 💡 **静态库的链接顺序**：`libbsp.a` 和 `libuser_library.a` 放在**目标文件之后**是必须的。链接器按顺序处理：先处理 `.o`（记下未解析的符号），再从左到右扫描 `.a` 补齐。如果 `.a` 放在前面，链接器扫过它时还不知道有谁需要它，就会全部丢弃 → 报未定义符号。

## 8.8 `CMakePresets.json` 与构建命令

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "default",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "toolchainFile": "${sourceDir}/cmake/gcc-arm-none-eabi.cmake",
            "cacheVariables": {}
        },
        {
            "name": "Debug",
            "inherits": "default",
            "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
        },
        {
            "name": "Release",
            "inherits": "default",
            "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
        }
    ],
    "buildPresets": [
        { "name": "Debug",   "configurePreset": "Debug" },
        { "name": "Release", "configurePreset": "Release" }
    ]
}
```

| 配置项 | 含义 |
|---|---|
| `generator: Ninja` | 用 Ninja 而非 Make —— 构建更快。需要 PATH 里有 `ninja`（CubeCLT 带） |
| `binaryDir: build/${presetName}` | Debug 产物在 `build/Debug/`，Release 在 `build/Release/` |
| `toolchainFile` | 自动套用 GCC 工具链文件，不用手动传 `-DCMAKE_TOOLCHAIN_FILE=` |

### 8.8.1 命令行构建

在**工程根目录**下执行（先把 CubeCLT 的 `CMake\bin`、`Ninja\bin`、`GNU-tools-for-STM32\bin` 加进 PATH）：

```bat
:: Debug 构建
cmake --preset Debug
cmake --build --preset Debug

:: Release 构建
cmake --preset Release
cmake --build --preset Release

:: 清理
cmake --build --preset Debug --target clean
```

想重新配置（比如改了 CMakeLists）时，直接再执行一次 `cmake --preset Debug` 即可。

### 8.8.2 不用 preset 的等价命令

如果你更喜欢显式参数：

```bat
cmake -S . -B build\Debug -G Ninja ^
      -DCMAKE_TOOLCHAIN_FILE=cmake\gcc-arm-none-eabi.cmake ^
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build\Debug
```

## 8.9 CLion 配置要点

| 项目 | 设置 |
|---|---|
| 打开工程 | `File → Open` → 选 `<工程根>` 目录（**不是** CMakeLists.txt 文件）|
| CMake Profile | CLion 会自动识别 `CMakePresets.json`，在 `Settings → Build, Execution, Deployment → CMake` 里选择 `Debug` 或 `Release` |
| Toolchain | 见 [2.4.4](#244-三种修法任选其一) 方法 C |
| 重新配置 | 改了 CMakeLists 后：`Tools → CMake → Reload CMake Project`；配置出错后：`Tools → CMake → Reset Cache and Reload Project` |

⚠️ **CLion 会自动格式化代码**（`Ctrl+Alt+L`，或保存时自动执行）。这会把 CubeMX 的 2 空格缩进改成 4 空格、大括号挪到同一行。功能上完全等价，但会让文档里的行号和实际对不上（见"阅读约定"的说明）。如果你不希望 CLion 动 CubeMX 生成的文件，可以在 `Settings → Editor → Code Style` 里把 `Core/` 目录设为例外。

## 8.10 构建产物与内存占用

构建成功后，产物在 `build/Debug/`（或 `build/Release/`）下：

| 文件 | 说明 |
|---|---|
| `STM32H750XBH6_Bootloader.elf` | **主产物**，含调试信息，可直接用于烧写和调试 |
| `STM32H750XBH6_Bootloader.bin` | 纯二进制镜像（无地址信息），由 POST_BUILD 步骤自动生成 |
| `STM32H750XBH6_Bootloader.hex` | Intel HEX 镜像（含地址信息），由 POST_BUILD 步骤自动生成 |
| `STM32H750XBH6_Bootloader.map` | 链接映射表，记录了每个函数/变量放在哪个地址、占多少字节 |
| `CMakeFiles/**/*.o` `*.su` | 中间目标文件、栈用量报告 |
| `compile_commands.json` | 编译数据库，给 IDE 做代码索引 |

✅ **已验证**：本工程实测的内存占用：

| 构建类型 | 编译选项 | FLASH 占用 | 占内部 Flash | DTCMRAM 占用 |
|---|---|---|---|---|
| **Debug** | `-O0 -g3` | **30824 B** | **23.52%** | 3272 B |
| **Release** | `-Os -g0` | **19364 B** | **14.77%** | 3272 B |

链接时会打印：

```
Memory region         Used Size  Region Size  %age Used
         DTCMRAM:        3272 B       128 KB      2.50%
             RAM:           0 B       512 KB      0.00%
          RAM_D2:           0 B       288 KB      0.00%
          RAM_D3:           0 B        64 KB      0.00%
         ITCMRAM:           0 B        64 KB      0.00%
           FLASH:       30824 B       128 KB     23.52%
```

> 💡 **RAM 为什么是 0**：boot 的全局变量和栈都放在 **DTCMRAM**（`0x20000000`），`STM32H750xx_FLASH.ld` 里把 `.data`/`.bss`/堆/栈都指向了 DTCM。AXI SRAM、D2、D3 这些 RAM 全部留给 app。

> 💡 **为什么 Debug 比 Release 大 11KB**：主要不是代码，而是 `-g3` 的调试信息被算进了 FLASH 报告。真正烧进芯片的字节数看 Release 更接近实情（约 19KB）。

---

# 第 9 章 烧写、运行与验证

## 9.1 构建产物与内存占用

见 [8.10](#810-构建产物与内存占用)。开始烧写前，先确认你手上有一个刚构建出来的 `STM32H750XBH6_Bootloader.elf`。

## 9.2 生成 .hex / .bin

CubeMX 生成的 CMake 工程**默认只产出 `.elf`**。`.elf` 可以直接被 STM32CubeProgrammer 烧写（它能解析 ELF 格式）；为了兼容只认 hex/bin 的烧写工具，**本工程已在顶层 `CMakeLists.txt` 的 `add_executable()` 之后添加了两个 `POST_BUILD` 步骤**（与 APP 工程写法一致），构建完成时自动用 `objcopy` 生成 `.bin` 和 `.hex`：

```cmake
#  在构建后，将ELF文件转换为BIN和HEX格式
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
        COMMENT "Generate bin file -> ${CMAKE_PROJECT_NAME}.bin"
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
        COMMENT "Generate hex file -> ${CMAKE_PROJECT_NAME}.hex"
)
```

`CMAKE_OBJCOPY` 已经在工具链文件里被设成 `arm-none-eabi-objcopy`，不用自己填路径。`.bin` / `.hex` 与 `.elf` 一起输出在 `build/Debug/`（或 `build/Release/`）下。

✅ **已验证**：Debug 构建 `.bin` 为 30824 B，与链接器报告的 FLASH 占用一致；`.hex` 首条扩展地址记录为 `:020000040800F2`，即基址 `0x08000000`。

> ⚠️ **`.bin` 没有地址信息**。烧 `.bin` 时必须手动指定起始地址 `0x08000000`，否则烧到错误位置。

## 9.3 用 STM32CubeProgrammer 烧写 boot

### 9.3.1 硬件连接

| 步骤 | 说明 |
|---|---|
| 1 | ST-Link 的 SWDIO/SWCLK/GND/3V3 接到核心板的 SWD 排针 |
| 2 | USB 转串口模块：模块的 **RX 接 PA9**（TX）、模块的 **TX 接 PA10**（RX）、GND 共地 |
| 3 | ST-Link 插上电脑 |

### 9.3.2 烧写步骤

| 步骤 | 操作 |
|---|---|
| 1 | 打开 **STM32CubeProgrammer** |
| 2 | 右上角选择接口：**ST-LINK**，点 **Connect** |
| 3 | 确认连上后，左侧选 **Erasing & Programming** |
| 4 | **File path** → 选择 `build\Debug\STM32H750XBH6_Bootloader.elf`（或 Release 版）|
| 5 | **Start address** 保持 `0x08000000`（烧 `.elf` 时 CubeProgrammer 会读文件里的地址信息，这一栏可能被自动处理）|
| 6 | 勾选 **Verify programming**（烧完自动校验，建议勾）|
| 7 | 点 **Start Programming** |
| 8 | 看到 "Download verified successfully" 即成功 |

📷 **【截图占位】** STM32CubeProgrammer 的 Erasing & Programming 页面。
需体现：接口选 ST-LINK 且已连接、File path 指向 .elf、Start address = 0x08000000、Start Programming 按钮。

### 9.3.3 命令行烧写（等价做法）

```bat
STM32_Programmer_CLI.exe -c port=SWD -w build\Debug\STM32H750XBH6_Bootloader.elf -v -rst
```

| 参数 | 含义 |
|---|---|
| `-c port=SWD` | 通过 SWD 连接 |
| `-w <file>` | 写入文件 |
| `-v` | 写完后校验 |
| `-rst` | 完成后复位芯片 |

### 9.3.4 ⚠️ 烧写时的注意事项

| 注意 | 说明 |
|---|---|
| **只烧 `0x08000000` 那 128KB** | 不要选 "Full chip erase" 之外的奇怪选项；也不要试图往 `0x90000000` 写 boot |
| **不要烧错地址** | boot 必须落在 `0x08000000`，这是 CPU 复位后取指令的地方 |
| **烧写前可以先全片擦除** | 如果之前烧过别的程序，建议先 `Erase` 一次，避免残留数据干扰 |
| **如果连接不上** | 检查 ST-Link 驱动；把 BOOT0 拉高后上电再连（进入系统 bootloader）；或降低 SWD 速度 |

✅ **已验证**：本工程的 boot 已按上述流程烧入真实硬件并运行通过。

## 9.4 连接串口与预期输出

### 9.4.1 串口参数

| 参数 | 值 |
|---|---|
| 端口 | 你的 USB 转串口模块对应的 COM 口 |
| 波特率 | **115200** |
| 数据位 | 8 |
| 校验 | None |
| 停止位 | 1 |
| 流控 | None |

### 9.4.2 预期输出

**给板子复位**（按复位键或在 CubeProgrammer 里点复位），串口应输出：

```
（空行）
***************************************
W25Q64 OK,flash ID:EF4017
Jump to the user application in W25Q64 >>>

```

逐行对照：

| # | 输出来源 | 说明 |
|---|---|---|
| 1 | `printf("\r\n***...***\r\n")` | 开头的 `\r\n` 产生那个空行 |
| 2 | `QSPI_W25Qxx_Init()` 成功分支 | **`EF4017` 必须完全一致** |
| 3 | `printf("Jump to the user application...")` | 打印完这句之后 boot 就跳走了 |
| 4 | 空行 | 字符串结尾是 `\r\n\r\n`。另外 GCC 有时会把 `printf` 优化成 `puts`（`puts` 会自动补一个换行），所以**可能看到两个空行** —— 这是正常的，不是错误 |

### 9.4.3 失败时的输出

如果 Flash 通信失败，会看到：

```
（空行）
***************************************
W25Q64 ERROR!!!!!  ID:0
W25Q64 not found, boot aborted.
```

然后程序停在 `while(1)`，不会有任何后续输出。

## 9.5 如何确认跳转真的成功了

有 4 种判断手段，从简单到深入：

| # | 方法 | 成功的表现 |
|---|---|---|
| 1 | **看串口输出** | 打印出 `Jump to the user application in W25Q64 >>>` 之后**不再有 boot 的输出**（boot 已经不运行了）|
| 2 | **看 app 的行为** | app 自己会跑起来（比如点灯、显示、串口打印它自己的启动信息）。**这是最直接的证据** |
| 3 | **用调试器看 PC** | 接上调试器，暂停，看 PC 是否在 `0x90000000` 以上（说明在 QSPI 里执行），或者看调用栈是否已经进入 app |
| 4 | **看 MSP** | 调试器里查看 MSP 寄存器，应该等于 app 向量表第 0 项的值（即 app 的栈顶，通常是 `0x2002xxxx` 或 `0x2408xxxx`）|

## 9.6 把 app 下载进 W25Q64

🚧 **本节为思路说明，本工程未实现自动下载功能。**

boot 只负责运行已经在 QSPI Flash 里的 app。那 app 是怎么进去的？有三种常见做法：

| 方法 | 说明 | 适用场景 |
|---|---|---|
| **① 用外部 Flash 下载算法** | STM32CubeProgrammer 支持"外部加载器"（External Loader，`.stldr` 文件）。针对你板子上的 QSPI Flash 装一个算法后，CubeProgrammer 就能像烧内部 Flash 一样直接烧 `0x90000000`。开发板厂商的例程包里通常带这类算法文件，也可以按芯片数据手册自己写 | **开发阶段最方便**，推荐 |
| **② 自己写一个下载功能** | 在 boot 里增加"通过串口/USB 接收固件 → 用 `QSPI_W25Qxx_XXX` 擦除并写入"的逻辑。本工程的 BSP 驱动**已经把擦除/编程函数都准备好了**（`SectorErase` / `BlockErase_64K` / `WriteBuffer`），只需再加一个通信协议和状态机 | 产品量产/现场升级 |
| **③ app 自己下载自己** | 有些方案让 app 运行期间接收新固件写入 QSPI 的另一块区域，再置标志让 boot 搬运 | 需要双备份设计 |

> 💡 **方法 ① 的关键**：下载算法本身也是一个运行在芯片上的程序，它需要把 QUADSPI 初始化好并用**间接模式**擦写（不能是内存映射模式）。本工程的 `BSP/Src/qspi_w25q64.c` 中的擦除/写入函数正是做这件事的，可以直接挪用到下载算法里。

📷 **【截图占位】** CubeProgrammer 的 External Loader 选择界面（如果你采用方法 ① 并成功用上了算法，把选择算法、地址填 `0x90000000` 的那一步截下来）。

---

# 第 10 章 关键设计决策

✅ **已验证**：本章把本工程中**所有"为什么这么做"的关键决策**集中列出，作为前面各章的索引式汇总，便于快速回顾或写代码时对照自查。

## 10.1 硬性约束

| # | 项目 | 正确做法 | 要点 | 详见 |
|---|---|---|---|---|
| 1 | QSPI 外设初始化 | **交给 CubeMX 生成**（`Core/Src/quadspi.c`），BSP 里只写芯片命令协议 | 两层职责分离，避免同名函数冲突 | 5.1 |
| 2 | printf 重定向 | 重写 **`_write()`**（Library 层）| GCC + newlib 的输出钩子是 `_write` | 6.1.1 |
| 3 | `syscalls.c` | 用 `#if 0` 注释掉 `_close`/`_fstat`/`_isatty`/`_lseek` | 这 4 个函数由 `Library/Src/retarget.c` 提供 | 6.3 |
| 4 | 启动文件 | 用 **GNU 汇编语法**的 `startup_stm32h750xx.s` | 与 GCC 工具链匹配 | 1.6.2 |
| 5 | 链接脚本 | 用 **GNU 语法**的 `.ld` | 与 GCC 工具链匹配 | 1.6.2 |
| 6 | `printf("%X", ...)` | 参数强转 `(unsigned int)` | GCC 下 `uint32_t` 是 `unsigned long` | 5.5 |
| 7 | **MPU 区域 1** | 设置 `AccessPermission = ALL ACCESS PERMITTED` | `0x90000000` 需要可读写、可执行 | **3.7** |
| 8 | **QSPI 引脚速度** | 6 根引脚设为 `Very High` | 驱动时钟 120MHz | **3.8.3** |
| 9 | 跳转用的 MSP / 入口地址 | 放在**全局变量**里 | 切换栈之后仍能取到值 | 7.4 |

## 10.2 本工程的设计选择

| # | 项目 | 本工程的做法 | 理由 | 详见 |
|---|---|---|---|---|
| 10 | 最终 `printf` 的位置 | 放在**关 SysTick 之前** | `HAL_UART_Transmit` 的超时依赖 SysTick | 7.2 步骤 5 |
| 11 | QSPI 初始化结果 | 检查 `QSPI_W25Qxx_Init()` 返回值 | 确认 Flash 通信正常后再跳转 | 7.2 步骤 3 |
| 12 | MSP 的存放位置 | 显式存入全局变量 | 任何优化等级下都安全，意图清晰 | 7.4 |
| 13 | LED | 不在 BSP 里重复配置 PC13，只保留控制宏 | PC13 的配置已由 CubeMX 的 `MX_GPIO_Init()` 完成，遵守职责划分 | 5.1 |
| 14 | 局部结构体 | 声明时 `= {0}` 清零 | 消除未初始化成员带来的不确定性 | 5.5 |
| 15 | printf 文案 | 用英文 | 源码是 UTF-8，中文在终端编码不一致时会乱码 | — |
| 16 | 跳转前关 MPU | 关掉 | 让 app 面对与"刚上电"一致的环境 | 7.6.4 |

## 10.3 不影响功能的细节

| # | 项目 | 说明 |
|---|---|---|
| 17 | USART1 的 AdvancedInit | 生成的是"接收溢出不报错 + 接收错误时禁用 DMA"。boot 只发不收，与这两个选项无关 |
| 18 | QSPI `MspInit` 里没有 `__HAL_RCC_QSPI_FORCE_RESET()` | 冷启动时 QSPI 本来就是复位状态，无需强制复位 |
| 19 | 源文件缩进风格 | CubeMX 生成的是 2 空格 + 大括号另起一行；用 IDE 格式化后可能变成 4 空格 + 大括号同行。功能完全等价，只影响本文档里的行号 |

---

# 第 11 章 移植与扩展

本章讲"如果你的硬件或需求不一样，要改哪些地方"。

> 🚧 **说明**：本章内容属于**方向性指导**，除第 11.1、11.2 两节的改动点是从本工程实际代码推导出来的之外，其余扩展功能**未在本工程中实现**。请把它当作出发点，具体实现需自行验证。

## 11.1 换成别的 QSPI Flash 芯片

假设你把 W25Q64（8MB）换成 W25Q128（16MB）或别的型号。

### 11.1.1 必须改的地方

| # | 位置 | 改什么 | 说明 |
|---|---|---|---|
| 1 | `BSP/Inc/qspi_w25q64.h` | `W25Qxx_FLASH_ID` | 改成新芯片的 JEDEC ID（查数据手册）。W25Q128 = `0xEF4018` |
| 2 | `BSP/Inc/qspi_w25q64.h` | `W25Qxx_FlashSize` | 新芯片的字节数。W25Q128 = `0x1000000`（16MB）|
| 3 | **CubeMX → QUADSPI → Flash Size** | 改容量指数 | 容量 = 2^(N+1)。**8MB → N=22；16MB → N=23** |
| 4 | `BSP/Src/qspi_w25q64.c` | `DummyCycles` | ⚠️ **不同芯片在 `0xEB` 命令下的 dummy 周期数可能不同**，必须查新芯片数据手册的 "Fast Read Quad I/O" 时序图 |
| 5 | `BSP/Inc/qspi_w25q64.h` | 所有命令码宏 | 只有命令码不同的芯片（比如某些 GD25/IS25）需要改；Winbond 系列基本兼容 |
| 6 | 擦除超时 | `W25Qxx_ChipErase_TIMEOUT_MAX` | 容量大了整片擦除更久，按新手册的最大值调大 |

### 11.1.2 不用改的地方

| 项 | 为什么不用改 |
|---|---|
| QUADSPI 的 8 个初始化参数（除 FlashSize）| 分频、采样点、CS 时间、时钟模式这些是**外设侧**的参数，与芯片型号无关（只要新芯片支持 Mode 3 和 120MHz）|
| 6 根引脚及复用号 | 硬件走线没变 |
| 内存映射地址 `0x90000000` | 由 QUADSPI Bank 1 决定，与芯片无关 |
| MPU 区域 1 | 大小 128MB 已经足够覆盖任何 ≤128MB 的 Flash |

## 11.2 换引脚 / 换到 Bank 2

### 11.2.1 只换引脚（Flash 还是接在 Bank 1 上）

在 CubeMX 的 QUADSPI 引脚分配里改，然后**重新生成代码**。别忘了：

| 注意 | 说明 |
|---|---|
| 新引脚的 **Maximum output speed 也要改成 Very High** | 每次改引脚后都要重新检查一遍（见 3.8.3）|
| 新引脚是否需要锁定（Locked）| 不锁的话 CubeMX 可能把它挪作他用 |

**代码层面不需要改任何东西** —— 引脚配置全在 CubeMX 生成的 `quadspi.c` 里。

### 11.2.2 换到 Bank 2

Bank 2 的映射地址是 **`0x70000000`**（不是 `0x90000000`）。需要三处联动修改：

| # | 位置 | 改成 |
|---|---|---|
| 1 | CubeMX → QUADSPI → Mode | 选 Bank 2 相关的模式；Parameter Settings 里 Flash ID 改 Bank 2 |
| 2 | `BSP/Inc/qspi_w25q64.h` | `#define W25Qxx_Mem_Addr 0x70000000` |
| 3 | CubeMX → CORTEX_M7 → MPU Region 1 | `BaseAddress` 改成 `0x70000000`（Size 视需要）|

> ⚠️ **`0x70000000` 落在 MPU 区域 0 的哪个子区？** `0x70000000` 在 SR3（`0x60000000`~`0x7FFFFFFF`）内，同样是 NO_ACCESS，所以**必须**有区域 1 来放行 —— 与 `0x90000000` 的情况完全一样。
> 💡 顺便记住：`BaseAddress = 0x90000000, Size = 128MB` 恰好是 128MB 对齐的；如果改成 `0x70000000`，`0x70000000 ÷ 0x08000000 = 14`，也整除 ✓。

### 11.2.3 用双 Flash（Dual Flash）

CubeMX 的 QUADSPI 里有 Dual Flash 选项，DualFlash 模式下数据线变成 8 根。需要 `hqspi.Init.DualFlash = QSPI_DUALFLASH_ENABLE` 并配置 `FlashID`。**本工程未使用**，不再展开。

## 11.3 改主频

如果你想降低主频（比如为了省电或降低布线要求）：

| 改什么 | 连带影响 |
|---|---|
| CubeMX → Clock Configuration → PLL 参数 | |
| **Voltage Scale** | 主频降到 400MHz 以下可以考虑 VOS1；**必须与主频匹配**，配错会起不来 |
| **Flash Latency** | CubeMX 会自动算 |
| **QSPI 驱动时钟** | QSPI 内核时钟 = HCLK，HCLK 变了 QSPI 驱动时钟跟着变。W25Q64 最高 133MHz，**确保不超过** |
| `SystemClock_Config()` | CubeMX 自动重新生成 |

> ⚠️ **如果 HCLK 变了导致 QSPI 驱动时钟超过 133MHz**，需要把 `ClockPrescaler` 调大（比如改成 2，则是 240/3 = 80MHz）。

## 11.4 扩展：加串口在线升级

🚧 **未实现，以下为思路。**

好消息是**底层驱动已经就绪**：`BSP/Src/qspi_w25q64.c` 里的 `SectorErase` / `BlockErase_64K` / `WriteBuffer` / `ReadBuffer` 都能直接用。

### 大致架构

```
上电
  │
  ├─ 检查某个标志（如 QSPI 起始扇区里的 magic number / 某个 RAM 备份寄存器）
  │
  ├─ 标志 = "要升级" → 进入升级模式
  │     ① 通过串口接收固件（自定义协议：包头 + 长度 + CRC + 数据）
  │     ② HAL_QSPI_Abort() 退出内存映射模式
  │     ③ BlockErase_64K() 擦除目标区域
  │     ④ WriteBuffer() 写入
  │     ⑤ 校验（CRC / 逐字节比对）
  │     ⑥ 清标志，复位
  │
  └─ 标志 = "正常启动" → 进入内存映射模式并跳转（本工程现有流程）
```

### 几个必须注意的点

| 注意 | 说明 |
|---|---|
| **写入前必须退出内存映射模式** | 映射模式只能读。用 `HAL_QSPI_Abort(&hqspi)` 退出，然后可能需要重新 `HAL_QSPI_Init()` |
| **Flash 必须"先擦后写"** | 见 [5.4.2（6）](#6-qspi_w25qxx_sectorerase--blockerase_32k--blockerase_64k--chiperase) |
| **别忘了 `WriteEnable`** | 每次擦/写之前都要发（`WriteBuffer` 内部已处理） |
| **升级过程要能容错** | 断电/断线不能变砖。做法：升级数据先写到"备份区"，全部校验通过后再改标志位 |
| **别擦掉自己** | boot 在内部 Flash，升级目标是 QSPI，天然安全 |
| **boot 的串口打印要留着** | 升级过程的可视化对排查问题极其重要 |

## 11.5 扩展：加 USB / SD 卡升级

🚧 **未实现。** 思路与 11.4 相同，只是把"串口接收"换成"USB MSC（把 QSPI 模拟成 U 盘）"或"读 SD 卡里的固件文件"。工程量比串口大得多（要移植 USB 协议栈），但底层 Flash 操作完全一样。

> ⚠️ 注意 Flash 容量预算：加 USB 栈后 boot 很容易超过 128KB 内部 Flash。**建议把升级功能单独做成一个独立的"升级专用 boot"，或用双 boot 设计**（一个极简的引导 + 一个带 USB 的升级器放在 QSPI 里）。

## 11.6 扩展：双备份与校验

🚧 **未实现。** 常见的可靠性设计：

| 方案 | 做法 | 优点 |
|---|---|---|
| **A/B 双区** | QSPI 前 4MB 放 A 版固件，后 4MB 放 B 版。boot 每次启动校验 A，坏了就启 B | 升级失败可回滚 |
| **固件头 + CRC** | 固件起始处放"魔数 + 长度 + CRC32"，boot 跳转前先校验 | 能发现固件损坏，避免跳进坏数据 |
| **备份寄存器标志** | 用 RTC 备份寄存器（`TAMP`）记录"正在升级/升级完成"状态 | 掉电后仍能恢复判断 |

> 💡 **在 boot 里加一个 CRC 校验非常简单，性价比很高**：读取 QSPI 的一段数据算 CRC 只多几百字节代码，却能避免"固件损坏 → 跳进去跑飞 → 无法再升级"的最坏情况。本工程为了保持流程最简，暂未加入。

## 11.7 移植到别的 STM32H7 型号

| 型号 | 内部 Flash | 要改什么 |
|---|---|---|
| STM32H743 / H753 | 1~2MB | 其实不需要 boot，程序直接放内部 Flash 即可 |
| STM32H750 / H7A3 | 128KB / 128KB | 与本工程相同思路 |
| STM32H723 / H725 | 最多 1MB | 可以不用 boot |
| STM32H563 / H573 | 2MB | 可以不用 boot |

**如果确实需要移植（比如换成 H7A3）**：

| # | 改什么 | 位置 |
|---|---|---|
| 1 | CubeMX 里重新选芯片 | 新建工程 |
| 2 | 时钟树 | H7A3 主频 280MHz，PLL 参数不同 |
| 3 | QSPI 参数 | 按新芯片的 Flash 手册调（见 11.1）|
| 4 | MPU 区域 1 的地址/大小 | 确认 QSPI 映射地址是否仍是 `0x90000000` |
| 5 | `CMakeLists.txt` 里的 `STM32H750xx` | 在 `cmake/stm32cubemx/CMakeLists.txt` 里（CubeMX 自动改）|
| 6 | `BSP/` 与 `Library/` | **完全可以复用**，只需确认 HAL API 没变 |

> 💡 **本工程最大的可复用价值就在 `BSP/` 和 `Library/`**：它们只依赖 HAL 的通用 API（`HAL_QSPI_*`、`HAL_UART_Transmit`），换成别的 H7 型号几乎不用改。

---

# 附录 A 文件清单与关键行号

> ⚠️ 行号会因 IDE 格式化/重新生成代码而漂移，请以"文件 + 函数名 + 代码内容"定位（见"阅读约定"）。

## A.1 CubeMX 生成的文件（`Core/`）

| 文件 | 关键位置 | 说明 |
|---|---|---|
| `Core/Inc/main.h` | 文件末尾 | `LED1_Pin` / `LED1_GPIO_Port` 宏 |
| `Core/Inc/stm32h7xx_hal_conf.h` | 第 67 行 | `HAL_QSPI_MODULE_ENABLED` |
| | 第 78 行 | `HAL_UART_MODULE_ENABLED` |
| `Core/Inc/quadspi.h` | 第 35 行 | `extern QSPI_HandleTypeDef hqspi;` |
| `Core/Inc/usart.h` | 第 35 行 | `extern UART_HandleTypeDef huart1;` |
| `Core/Src/main.c` | 26~32 | `USER CODE BEGIN Includes`（三个 include）|
| | 34~37 | `USER CODE BEGIN PTD`（`pFunction` 类型定义）|
| | 49~56 | `USER CODE BEGIN PV`（`JumpToApplication` / `AppStackPointer`）|
| | 82 | `MPU_Config();` 调用 |
| | 85~91 附近 | `SCB_EnableICache()` / `SCB_EnableDCache()` |
| | 109~111 | `MX_GPIO_Init()` / `MX_QUADSPI_Init()` / `MX_USART1_UART_Init()` |
| | **112~149** | **★ `USER CODE BEGIN 2` —— 引导流程** |
| | **223~259** | **★ `MPU_Config()` —— 区域 0 + 区域 1** |
| | 231~243 | 区域 0（4GB 背景保护区）|
| | **247~256** | **★ 区域 1（0x90000000 放行）** |
| `Core/Src/quadspi.c` | 27 | `QSPI_HandleTypeDef hqspi;` |
| | 30~57 | `MX_QUADSPI_Init()` |
| | 40~48 | ★ 8 个 QSPI 参数 |
| | 59~117 | `HAL_QSPI_MspInit()` |
| | 72~77 | ★ QSPI 内核时钟 = D1HCLK |
| | 92~111 | ★ 6 根引脚配置（三处 Speed 必须 VERY_HIGH）|
| `Core/Src/usart.c` | 31~74 | `MX_USART1_UART_Init()` |
| | 41~53 | UART 参数 |
| | 58~69 | FIFO 关闭 |
| | 76~115 | `HAL_UART_MspInit()`（PA9/PA10 + AF7）|
| `Core/Src/gpio.c` | 41~63 | `MX_GPIO_Init()`（PC13/LED1）|
| `Core/Src/stm32h7xx_it.c` | 99~105 | `MemManage_Handler()`（`while(1)` 在第 104 行）|
| `Core/Src/syscalls.c` | **92~126** | **★ `#if 0` 包住 4 个函数** |
| `Core/Src/sysmem.c` | — | `_sbrk()` 堆管理，不用改 |

## A.2 手写文件（`BSP/` 与 `Library/`）

| 文件 | 关键位置 | 说明 |
|---|---|---|
| `BSP/Inc/led.h` | 全文 | LED 控制宏（无 .c）|
| `BSP/Inc/qspi_w25q64.h` | 约 30~64 | 返回值宏 / 命令码宏 / 容量宏 |
| | 第 62 行附近 | `W25Qxx_Mem_Addr = 0x90000000` |
| | 66~78 | 11 个对外函数声明 |
| `BSP/Src/qspi_w25q64.c` | 40 | `QSPI_W25Qxx_Init` |
| | 69 | `QSPI_W25Qxx_AutoPollingMemReady`（内部）|
| | 111 | `QSPI_W25Qxx_Reset` |
| | 159 | `QSPI_W25Qxx_ReadID` |
| | **201** | **★ `QSPI_W25Qxx_MemoryMappedMode`** |
| | 238 | `QSPI_W25Qxx_WriteEnable`（内部）|
| | 299 / 351 / 404 / 456 | 四种擦除 |
| | 528 / 592 | 写入 |
| | 666 | 读取 |
| `BSP/CMakeLists.txt` | 全文 | `bsp` 静态库 |
| `Library/Inc/retarget.h` | 全文 | 6 个桩函数 + `RetargetInit` 声明 |
| `Library/Src/retarget.c` | 15 | `UART_HandleTypeDef *gHuart;` |
| | 17 | `RetargetInit()` |
| | **33** | **★ `_write()`** |
| | 25 / 47 / 55 / 64 / 78 | 其余 5 个桩函数 |
| `Library/CMakeLists.txt` | 全文 | `user_library` 静态库 |

## A.3 构建脚本

| 文件 | 关键位置 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 1 | `cmake_minimum_required(VERSION 3.22)` |
| | 28 | `project(...)` |
| | **40~44** | **★ `add_subdirectory(BSP)` / `add_subdirectory(Library)`** |
| | **70~76** | **★ `target_link_libraries(... bsp user_library)`** |
| `cmake/stm32cubemx/CMakeLists.txt` | 22~30 | 应用源文件列表（**每次重写，勿改**）|
| | 35~53+ | HAL 驱动列表 |
| `cmake/gcc-arm-none-eabi.cmake` | 9~16 | ★ 编译器定义（**裸名字，靠 PATH**）|
| | 25 | `TARGET_FLAGS`（Cortex-M7 + FPU）|
| | 35~38 | Debug/Release 优化等级 |
| | 43~46 | 链接选项（`-T` 链接脚本、nano.specs、map、gc-sections）|
| `CMakePresets.json` | 全文 | Debug/Release 预设 |
| `STM32H750xx_FLASH.ld` | **58~63** | **★ MEMORY 段（地址布局）** |
| | 70~71 | `_Min_Heap_Size` / `_Min_Stack_Size` |

---

# 附录 B 关键地址与寄存器速查

## B.1 存储器与地址

| 地址 | 名称 | 大小 | 用途 |
|---|---|---|---|
| `0x0000_0000` | ITCM RAM（或 Flash 别名）| 64KB | 未使用 |
| **`0x0800_0000`** | **内部 Flash** | **128KB** | **bootloader 自己** |
| `0x2000_0000` | DTCM RAM | 128KB | boot 的栈/堆/全局变量 |
| `0x2400_0000` | AXI SRAM (D1) | 512KB | 留给 app |
| `0x3000_0000` | AHB SRAM (D2) | 288KB | 留给 app |
| `0x3800_0000` | AHB SRAM (D3) | 64KB | 留给 app |
| `0x5200_5000` | QUADSPI 外设寄存器 | — | |
| `0x5802_0800` | GPIOC | — | PC13 = LED |
| `0x5802_1400` | GPIOF | — | QSPI 的 5 根脚 |
| `0x5802_1800` | GPIOG | — | QSPI 的 NCS |
| `0x4001_1000` | USART1 | — | |
| `0x5802_4400` | RCC | — | |
| **`0x9000_0000`** | **QUADSPI Bank 1 内存映射区** | 最大 256MB | **app 所在位置** |
| `0x7000_0000` | QUADSPI Bank 2 内存映射区 | — | 本工程未用 |
| `0xE000_E000` | SCB / SysTick 系统控制区 | — | |

## B.2 关键寄存器

| 寄存器 | 地址 | 本工程用到的位 | 说明 |
|---|---|---|---|
| `SCB->VTOR` | `0xE000_ED08` | — | 向量表偏移，**本工程未修改** |
| `SCB->CCR` | `0xE000_ED14` | bit16 DC / bit17 IC | D-Cache / I-Cache 使能 |
| `SCB->SHCSR` | `0xE000_ED24` | bit16 MEMFAULTENA | MemManage 异常使能 |
| `MPU->CTRL` | `0xE000_ED94` | bit0 ENABLE / bit1 HFNMIENA / bit2 PRIVDEFENA | `MPU_PRIVILEGED_DEFAULT` → 值 `0x05` |
| `MPU->RNR` | `0xE000_ED98` | — | 选择要配置的区域编号 |
| `MPU->RBAR` | `0xE000_ED9C` | — | 区域基地址 |
| `MPU->RASR` | `0xE000_EDA0` | 见下 | 区域属性 |
| `SysTick->CTRL` | `0xE000_E010` | bit0 ENABLE / bit1 TICKINT / bit2 CLKSOURCE | |
| `SysTick->LOAD` | `0xE000_E014` | — | 重载值 |
| `SysTick->VAL` | `0xE000_E018` | — | 当前值 |

### RASR 字段构成（`MPU->RASR`）

| 位域 | 名称 | 本工程区域 0 | 本工程区域 1 |
|---|---|---|---|
| [28] | XN（禁止取指）| 1（禁止）| **0（允许）** |
| [26:24] | AP（访问权限）| 000 = NO_ACCESS | **011 = FULL_ACCESS** |
| [21:19] | TEX | 000 | 000 |
| [18] | S（共享）| 1 | 0 |
| [17] | C（可缓存）| 0 | **1** |
| [16] | B（可缓冲）| 0 | 0 |
| [15:8] | SRD（子区禁用）| **0x87** | 0x00 |
| [5:1] | SIZE | 0x1F = 4GB | 0x1A = 128MB |
| [0] | ENABLE | 1 | 1（沿用）|

> 计算出的最终值（可用于反汇编核对）：区域 0 = `0x1004873F`，区域 1 = `0x03020035`。
> ⚠️ 这两个值**不会直接出现在二进制里** —— `HAL_MPU_ConfigRegion()` 是在运行时用结构体字段拼出 RASR 的。要做二进制核对请看 [4.3.2](#432-确认-mpu-区域-1-真的配置对了-强烈建议做一次) 的逐字段反汇编。

## B.3 时钟树

```
HSE 25MHz
  │
  └─÷5 (DIVM1)──▶ 5MHz ──×192 (DIVN1)──▶ 960MHz (VCO)
                                              │
                                              └─÷2 (DIVP1)──▶ 480MHz ──▶ SYSCLK (Cortex-M7 内核)
                                                                             │
                                                                             └─÷2 (HPRE)──▶ 240MHz = HCLK
                                                                                              │
                                                                                              ├─÷2──▶ 120MHz APB1/2/3/4
                                                                                              │
                                                                                              └────▶ QUADSPI 内核时钟 240MHz
                                                                                                       └─÷(1+1)──▶ 120MHz QSPI 驱动时钟
```

| 时钟 | 频率 | 用途 |
|---|---|---|
| SYSCLK / CPU | 480 MHz | 内核 |
| HCLK / AXI | 240 MHz | 总线、**QUADSPI 内核时钟** |
| APB1/2/3/4 | 120 MHz | 外设（USART1 用 APB2 = 120MHz）|
| QSPI 驱动时钟 | **120 MHz** | W25Q64 最高 133MHz ✓ |

---

# 附录 C ARMv7-M 默认内存映射表

MPU 未覆盖的地址区域使用下表（也就是 `HAL_MPU_Disable()` 之后所有访问遵循的属性）。来源：ARMv7-M Architecture Reference Manual（DDI 0403）Table B3-1。

| 地址范围 | 名称 | 内存类型 | 可执行 | 子区编号<br>(区域 0 的 512MB 子区) |
|---|---|---|---|---|
| `0x0000_0000`~`0x1FFF_FFFF` | Code | Normal, WT | ✅ | SR0（被 0x87 排除）|
| `0x2000_0000`~`0x3FFF_FFFF` | SRAM | Normal, WBWA | ✅ | SR1（被排除）|
| `0x4000_0000`~`0x5FFF_FFFF` | Peripheral | Device | ❌ | SR2（被排除）|
| `0x6000_0000`~`0x7FFF_FFFF` | External RAM | Normal, WBWA | ✅ | **SR3（NO_ACCESS）** |
| `0x8000_0000`~`0x9FFF_FFFF` | External RAM | **Normal, WT** | ✅ | **SR4（NO_ACCESS）← 0x90000000 在这里** |
| `0xA000_0000`~`0xBFFF_FFFF` | External device | Device | ❌ | **SR5（NO_ACCESS）** |
| `0xC000_0000`~`0xDFFF_FFFF` | External device | Device | ❌ | **SR6（NO_ACCESS）** |
| `0xE000_0000`~`0xE00F_FFFF` | Private peripheral bus | Strongly-ordered | ❌ | SR7（被排除）|
| `0xE010_0000`~`0xFFFF_FFFF` | Vendor / system | Device | ❌ | SR7（被排除）|

**两张关键结论**：

1. **`0x90000000` 在默认映射下是 Normal / Write-Through / 可执行** —— 这解释了为什么：
   - 一个完全不配置 MPU 的引导程序也能正常从 QSPI 取指令执行；
   - 本工程给 MPU 区域 1 配的属性（TEX=0, C=1, B=0）与默认映射**完全一致**；
   - 因此**关掉 MPU 不会影响 app 在 QSPI 里运行**（见 [7.6.3](#763-关键结论对-qspi-区而言关不关没区别)）。
2. **`0x60000000`~`0xDFFFFFFF` 在 MPU 区域 0 生效期间是"禁止访问"** —— 这是区域 0 的作用范围，也是"跳转前要不要关 MPU"这个选择题的关键（见 [7.6.4](#764-那到底该关还是不该关)）。

---

## 附录 D .ioc 文件全文

下面是本工程的 `STM32H750XBH6_Bootloader.ioc` 全文。它是**纯文本**的配置存档，可以直接用文本工具打开、比对、纳入 Git 版本管理。

> 💡 **怎么用它快速复现**：在 CubeMX 里新建一个空工程（选 STM32H750XBHx），关闭 CubeMX，用下面的内容**覆盖**新工程同名的 `.ioc` 文件，再打开该 `.ioc` —— CubeMX 会读出全部配置，直接 `GENERATE CODE` 即可。
> ⚠️ 覆盖前请确认 `ProjectManager.ProjectName` 与文件名一致。

```ini
#MicroXplorer Configuration settings - do not modify
CAD.formats=
CAD.pinconfig=
CAD.provider=
CORTEX_M7.AccessPermission_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_REGION_FULL_ACCESS
CORTEX_M7.BaseAddress_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=0x90000000
CORTEX_M7.CPU_DCache=Enabled
CORTEX_M7.CPU_ICache=Enabled
CORTEX_M7.Enable_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_REGION_ENABLE
CORTEX_M7.IPParameters=default_mode_Activation,CPU_ICache,CPU_DCache,Enable_S-Cortex_Memory_Protection_Unit_Region1_Settings_S,BaseAddress_S-Cortex_Memory_Protection_Unit_Region1_Settings_S,Size_S-Cortex_Memory_Protection_Unit_Region1_Settings_S,IsCacheable_S-Cortex_Memory_Protection_Unit_Region1_Settings_S,AccessPermission_S-Cortex_Memory_Protection_Unit_Region1_Settings_S
CORTEX_M7.IsCacheable_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_ACCESS_CACHEABLE
CORTEX_M7.Size_S-Cortex_Memory_Protection_Unit_Region1_Settings_S=MPU_REGION_SIZE_128MB
CORTEX_M7.default_mode_Activation=1
File.Version=6
GPIO.groupedBy=Group By Peripherals
KeepUserPlacement=false
MMTAppRegionsCount=0
MMTConfigApplied=false
MMTSectionSuffix=
Mcu.CPN=STM32H750XBH6
Mcu.Family=STM32H7
Mcu.IP0=CORTEX_M7
Mcu.IP1=DEBUG
Mcu.IP2=MEMORYMAP
Mcu.IP3=NVIC
Mcu.IP4=QUADSPI
Mcu.IP5=RCC
Mcu.IP6=SYS
Mcu.IP7=USART1
Mcu.IPNb=8
Mcu.Name=STM32H750XBHx
Mcu.Package=TFBGA240
Mcu.Pin0=PA14 (JTCK/SWCLK)
Mcu.Pin1=PA13 (JTMS/SWDIO)
Mcu.Pin10=PF8
Mcu.Pin11=PF10
Mcu.Pin12=PF9
Mcu.Pin13=VP_SYS_VS_Systick
Mcu.Pin14=VP_MEMORYMAP_VS_MEMORYMAP
Mcu.Pin2=PA10
Mcu.Pin3=PA9
Mcu.Pin4=PC13
Mcu.Pin5=PG6
Mcu.Pin6=PH1-OSC_OUT (PH1)
Mcu.Pin7=PH0-OSC_IN (PH0)
Mcu.Pin8=PF6
Mcu.Pin9=PF7
Mcu.PinsNb=15
Mcu.ThirdPartyNb=0
Mcu.UserConstants=
Mcu.UserName=STM32H750XBHx
MxCube.Version=6.18.1
MxDb.Version=DB.6.0.181
NVIC.BusFault_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.DebugMonitor_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.ForceEnableDMAVector=true
NVIC.HardFault_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.MemoryManagement_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.NonMaskableInt_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.PendSV_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.PriorityGroup=NVIC_PRIORITYGROUP_4
NVIC.SVCall_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
NVIC.SysTick_IRQn=true\:15\:0\:false\:false\:true\:false\:true\:false
NVIC.UsageFault_IRQn=true\:0\:0\:false\:false\:true\:false\:false\:false
PA10.Locked=true
PA10.Mode=Asynchronous
PA10.Signal=USART1_RX
PA13\ (JTMS/SWDIO).Mode=Serial_Wire
PA13\ (JTMS/SWDIO).Signal=DEBUG_JTMS-SWDIO
PA14\ (JTCK/SWCLK).Mode=Serial_Wire
PA14\ (JTCK/SWCLK).Signal=DEBUG_JTCK-SWCLK
PA9.Locked=true
PA9.Mode=Asynchronous
PA9.Signal=USART1_TX
PC13.GPIOParameters=GPIO_Label
PC13.GPIO_Label=LED1
PC13.Locked=true
PC13.Signal=GPIO_Output
PF10.GPIOParameters=GPIO_Speed
PF10.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF10.Mode=Single Bank 1
PF10.Signal=QUADSPI_CLK
PF6.GPIOParameters=GPIO_Speed
PF6.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF6.Mode=Single Bank 1
PF6.Signal=QUADSPI_BK1_IO3
PF7.GPIOParameters=GPIO_Speed
PF7.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF7.Locked=true
PF7.Mode=Single Bank 1
PF7.Signal=QUADSPI_BK1_IO2
PF8.GPIOParameters=GPIO_Speed
PF8.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF8.Locked=true
PF8.Mode=Single Bank 1
PF8.Signal=QUADSPI_BK1_IO0
PF9.GPIOParameters=GPIO_Speed
PF9.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PF9.Locked=true
PF9.Mode=Single Bank 1
PF9.Signal=QUADSPI_BK1_IO1
PG6.GPIOParameters=GPIO_Speed
PG6.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH
PG6.Locked=true
PG6.Mode=Single Bank 1
PG6.Signal=QUADSPI_BK1_NCS
PH0-OSC_IN\ (PH0).Mode=HSE-External-Oscillator
PH0-OSC_IN\ (PH0).Signal=RCC_OSC_IN
PH1-OSC_OUT\ (PH1).Mode=HSE-External-Oscillator
PH1-OSC_OUT\ (PH1).Signal=RCC_OSC_OUT
PinOutPanel.CurrentBGAView=Top
PinOutPanel.RotationAngle=0
ProjectManager.AskForMigrate=true
ProjectManager.BackupPrevious=false
ProjectManager.CompilerLinker=GCC
ProjectManager.CompilerOptimize=6
ProjectManager.ComputerToolchain=false
ProjectManager.CoupleFile=true
ProjectManager.CustomerFirmwarePackage=
ProjectManager.DefaultFWLocation=true
ProjectManager.DeletePrevious=true
ProjectManager.DeviceId=STM32H750XBHx
ProjectManager.FirmwarePackage=STM32Cube FW_H7 V1.13.0
ProjectManager.FreePins=false
ProjectManager.FreePinsContext=
ProjectManager.HalAssertFull=false
ProjectManager.HeapSize=0x400
ProjectManager.KeepUserCode=true
ProjectManager.LastFirmware=true
ProjectManager.LibraryCopy=1
ProjectManager.MainLocation=Core/Src
ProjectManager.NoMain=false
ProjectManager.PreviousToolchain=
ProjectManager.ProjectBuild=false
ProjectManager.ProjectFileName=STM32H750XBH6_Bootloader.ioc
ProjectManager.ProjectName=STM32H750XBH6_Bootloader
ProjectManager.ProjectStructure=
ProjectManager.RegisterCallBack=
ProjectManager.StackSize=0x600
ProjectManager.TargetToolchain=CMake
ProjectManager.ToolChainLocation=
ProjectManager.UAScriptAfterPath=
ProjectManager.UAScriptBeforePath=
ProjectManager.UnderRoot=false
ProjectManager.functionlistsort=1-SystemClock_Config-RCC-false-HAL-false,2-MX_GPIO_Init-GPIO-false-HAL-true,3-MX_QUADSPI_Init-QUADSPI-false-HAL-true,4-MX_USART1_UART_Init-USART1-false-HAL-true,0-MX_CORTEX_M7_Init-CORTEX_M7-false-HAL-true
QUADSPI.ClockMode=QSPI_CLOCK_MODE_3
QUADSPI.ClockPrescaler=1
QUADSPI.FifoThreshold=32
QUADSPI.FlashSize=22
QUADSPI.IPParameters=ClockPrescaler,FifoThreshold,SampleShifting,FlashSize,ClockMode
QUADSPI.SampleShifting=QSPI_SAMPLE_SHIFTING_HALFCYCLE
RCC.ADCFreq_Value=480000000
RCC.AHB12Freq_Value=240000000
RCC.AHB4Freq_Value=240000000
RCC.APB1Freq_Value=120000000
RCC.APB2Freq_Value=120000000
RCC.APB3Freq_Value=120000000
RCC.APB4Freq_Value=120000000
RCC.AXIClockFreq_Value=240000000
RCC.CECFreq_Value=32000
RCC.CKPERFreq_Value=64000000
RCC.CortexFreq_Value=480000000
RCC.CpuClockFreq_Value=480000000
RCC.D1CPREFreq_Value=480000000
RCC.D1PPRE=RCC_APB3_DIV2
RCC.D2PPRE1=RCC_APB1_DIV2
RCC.D2PPRE2=RCC_APB2_DIV2
RCC.D3PPRE=RCC_APB4_DIV2
RCC.DFSDMACLkFreq_Value=480000000
RCC.DFSDMFreq_Value=120000000
RCC.DIVM1=5
RCC.DIVM2=5
RCC.DIVM3=5
RCC.DIVN1=192
RCC.DIVN2=192
RCC.DIVN3=192
RCC.DIVP1Freq_Value=480000000
RCC.DIVP2Freq_Value=480000000
RCC.DIVP3Freq_Value=480000000
RCC.DIVQ1Freq_Value=480000000
RCC.DIVQ2Freq_Value=480000000
RCC.DIVQ3Freq_Value=480000000
RCC.DIVR1Freq_Value=480000000
RCC.DIVR2Freq_Value=480000000
RCC.DIVR3Freq_Value=480000000
RCC.FDCANFreq_Value=480000000
RCC.FMCFreq_Value=240000000
RCC.FamilyName=M
RCC.HCLK3ClockFreq_Value=240000000
RCC.HCLKFreq_Value=240000000
RCC.HPRE=RCC_HCLK_DIV2
RCC.HRTIMFreq_Value=240000000
RCC.I2C123Freq_Value=120000000
RCC.I2C4Freq_Value=120000000
RCC.IPParameters=ADCFreq_Value,AHB12Freq_Value,AHB4Freq_Value,APB1Freq_Value,APB2Freq_Value,APB3Freq_Value,APB4Freq_Value,AXIClockFreq_Value,CECFreq_Value,CKPERFreq_Value,CortexFreq_Value,CpuClockFreq_Value,D1CPREFreq_Value,D1PPRE,D2PPRE1,D2PPRE2,D3PPRE,DFSDMACLkFreq_Value,DFSDMFreq_Value,DIVM1,DIVM2,DIVM3,DIVN1,DIVN2,DIVN3,DIVP1Freq_Value,DIVP2Freq_Value,DIVP3Freq_Value,DIVQ1Freq_Value,DIVQ2Freq_Value,DIVQ3Freq_Value,DIVR1Freq_Value,DIVR2Freq_Value,DIVR3Freq_Value,FDCANFreq_Value,FMCFreq_Value,FamilyName,HCLK3ClockFreq_Value,HCLKFreq_Value,HPRE,HRTIMFreq_Value,I2C123Freq_Value,I2C4Freq_Value,LPTIM1Freq_Value,LPTIM2Freq_Value,LPTIM345Freq_Value,LPUART1Freq_Value,LTDCFreq_Value,MCO1PinFreq_Value,MCO2PinFreq_Value,PLLFRACN,PLLSourceVirtual,QSPIFreq_Value,RNGFreq_Value,RTCFreq_Value,SAI1Freq_Value,SAI23Freq_Value,SAI4AFreq_Value,SAI4BFreq_Value,SDMMCFreq_Value,SPDIFRXFreq_Value,SPI123Freq_Value,SPI45Freq_Value,SPI6Freq_Value,SWPMI1Freq_Value,SYSCLKFreq_VALUE,SYSCLKSource,Tim1OutputFreq_Value,Tim2OutputFreq_Value,TraceFreq_Value,USART16Freq_Value,USART234578Freq_Value,USBFreq_Value,VCO1OutputFreq_Value,VCO2OutputFreq_Value,VCO3OutputFreq_Value,VCOInput1Freq_Value,VCOInput2Freq_Value,VCOInput3Freq_Value
RCC.LPTIM1Freq_Value=120000000
RCC.LPTIM2Freq_Value=120000000
RCC.LPTIM345Freq_Value=120000000
RCC.LPUART1Freq_Value=120000000
RCC.LTDCFreq_Value=480000000
RCC.MCO1PinFreq_Value=64000000
RCC.MCO2PinFreq_Value=480000000
RCC.PLLFRACN=0
RCC.PLLSourceVirtual=RCC_PLLSOURCE_HSE
RCC.QSPIFreq_Value=240000000
RCC.RNGFreq_Value=48000000
RCC.RTCFreq_Value=32000
RCC.SAI1Freq_Value=480000000
RCC.SAI23Freq_Value=480000000
RCC.SAI4AFreq_Value=480000000
RCC.SAI4BFreq_Value=480000000
RCC.SDMMCFreq_Value=480000000
RCC.SPDIFRXFreq_Value=480000000
RCC.SPI123Freq_Value=480000000
RCC.SPI45Freq_Value=120000000
RCC.SPI6Freq_Value=120000000
RCC.SWPMI1Freq_Value=120000000
RCC.SYSCLKFreq_VALUE=480000000
RCC.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK
RCC.Tim1OutputFreq_Value=240000000
RCC.Tim2OutputFreq_Value=240000000
RCC.TraceFreq_Value=480000000
RCC.USART16Freq_Value=120000000
RCC.USART234578Freq_Value=120000000
RCC.USBFreq_Value=480000000
RCC.VCO1OutputFreq_Value=960000000
RCC.VCO2OutputFreq_Value=960000000
RCC.VCO3OutputFreq_Value=960000000
RCC.VCOInput1Freq_Value=5000000
RCC.VCOInput2Freq_Value=5000000
RCC.VCOInput3Freq_Value=5000000
USART1.DMADisableonRxErrorParam=ADVFEATURE_DMA_DISABLEONRXERROR
USART1.IPParameters=VirtualMode-Asynchronous,OverrunDisableParam,DMADisableonRxErrorParam
USART1.OverrunDisableParam=ADVFEATURE_OVERRUN_DISABLE
USART1.VirtualMode-Asynchronous=VM_ASYNC
VP_MEMORYMAP_VS_MEMORYMAP.Mode=CurAppReg
VP_MEMORYMAP_VS_MEMORYMAP.Signal=MEMORYMAP_VS_MEMORYMAP
VP_SYS_VS_Systick.Mode=SysTick
VP_SYS_VS_Systick.Signal=SYS_VS_Systick
board=custom
```


---

# 附录 E 版本记录

## E.1 软件版本（本次验证环境）

| 软件 | 版本 | 备注 |
|---|---|---|
| STM32CubeMX | **6.18.1** | `.ioc` 里记录为 `MxCube.Version=6.18.1` |
| CubeMX 器件数据库 | DB.6.0.181 | `MxDb.Version=DB.6.0.181` |
| STM32Cube FW_H7 | **V1.13.0** | `ProjectManager.FirmwarePackage=STM32Cube FW_H7 V1.13.0` |
| STM32CubeCLT | **1.22.0** | 安装路径 `C:\Program Files\STMicroelectronics\STM32CubeCLT_1.22.0` |
| arm-none-eabi-gcc | **14.3.1** | `GNU Tools for STM32 14.3.rel1.20251027-0700` |
| CMake | **4.3.1** | 随 CubeCLT 安装 |
| Ninja | 随 CubeCLT | |
| CLion | 2026.2.3.1 | 可选 |
| 操作系统 | Windows | 路径示例均为 Windows 风格 |

## E.2 文档修订记录

| 版本 | 日期 | 修订内容 |
|---|---|---|
| v1.0 | — | 初版：完整覆盖从环境准备到烧写验证的全流程，含 MPU 区域 1、QSPI 引脚速度等重点配置的详细说明 |
| v1.1 | 2026-10-15 | 与工程现状同步：§9.2 改为工程已内置 bin/hex 生成（POST_BUILD）并补充实测结果；§8.10 产物表与 §8 构建图补 .bin/.hex；§0.3 目录树修正文档位置说明（仓库 `1.Docs/`） |

## E.3 构建产物记录

| 构建类型 | FLASH | DTCMRAM | 说明 |
|---|---|---|---|
| Debug (`-O0 -g3`) | 30824 B（23.52%）| 3272 B | 含调试信息 |
| Release (`-Os -g0`) | 19364 B（14.77%）| 3272 B | 适合量产 |

---

# 附录 F 参考资料

## F.1 ST 官方文档与代码

| 资料 | 位置/编号 | 用途 |
|---|---|---|
| STM32H750XB 数据手册 | ST 官网 | 引脚复用表（AF9/AF10）、电气特性 |
| STM32H7 参考手册 | RM0433 | QUADSPI、RCC、MPU 寄存器细节 |
| **AN4838** Introduction to memory protection unit management on STM32 MCUs | ST 官网 | MPU 子区、`SubRegionDisable` 的官方说明 |
| **ST 官方 QSPI bootloader 例程**<br>`ExtMem_Boot` | `STM32Cube_FW_H7_V1.13.0\Projects\STM32H743I-EVAL\Applications\ExtMem_CodeExecution\ExtMem_Boot\` | ★ **与本工程最接近的官方实现**：区域 0 背景保护区 + QSPI 区域 + 关缓存 + 跳转，本工程的 MPU 方案即参照它 |
| ST 官方 QSPI 例程 | `...\Projects\STM32H743I-EVAL\Examples\QSPI\QSPI_ExecuteInPlace\` | QSPI 内存映射 + XIP 的官方写法 |
| ST 官方 QSPI MSP 参考 | `ExtMem_Boot\Src\memory_msp.c` | QSPI 引脚速度用 `VERY_HIGH` 的官方依据 |

## F.2 芯片手册

| 资料 | 用途 |
|---|---|
| **W25Q64JV 数据手册**（Winbond）| ★ 命令码表、`0xEB` 的 dummy 周期数、擦除/编程时间、"先擦后写"规则、JEDEC ID |
| ARMv7-M Architecture Reference Manual (DDI 0403) | 默认内存映射表（附录 C）、MPU RASR 位域、弱符号无关（这是工具链层面）|

## F.3 本工程中第三方代码的来源

| 来源 | 涉及的代码 | 说明 |
|---|---|---|
| **反客（FANKE）公开的 W25Q64 驱动** | `BSP/Src/qspi_w25q64.c` 的命令协议部分 | 本工程在该驱动基础上整理：删除其中属于"外设配置"的 `MX_QUADSPI_Init()` / `HAL_QSPI_MspInit()`（改由 CubeMX 生成），只保留命令协议层，并按本工程工具链做了适配。逐条改动见 [5.5](#55-w25qxx-驱动的来源与改动说明) |
| STM32CubeMX / STM32Cube FW_H7 | `Core/`、`Drivers/`、`cmake/` | 外设初始化代码、HAL 库、CMSIS、构建脚本，均由工具生成（授权条款见各目录下的 LICENSE 文件）|
| 用户库 | `Library/` | printf → UART 的 newlib 重定向实现 |

> ⚠️ **除上表所列，本工程其余代码（引导流程、CMake 组织、本文档）均为本项目自有实现。** 引用第三方代码时请遵守其原始授权条款。

## F.4 工具与链接

| 工具 | 用途 |
|---|---|
| `arm-none-eabi-size` | 查看固件大小 |
| `arm-none-eabi-nm` | 查看符号表（确认函数在哪、有没有被内联）|
| `arm-none-eabi-objdump` | 反汇编、查看段布局 |
| STM32CubeProgrammer | 烧写、读回 Flash |
| `.map` 文件 | 分析每个函数/变量占用的地址和大小 |

---

**文档结束。**

> 📌 **最后提醒**：请务必确认两处配置 —— **3.7 的 MPU 区域 1**（`AccessPermission` 选 `ALL ACCESS PERMITTED`）与 **3.8.3 的引脚速度**（6 根 QSPI 引脚设为 `Very High`）。这两项都不容易从表面上判断，建议按第 4 章的核对清单逐项确认。

