# STM32 USB 虚拟串口 (CDC) —— 移植与使用

本文档介绍如何把本工程（`STM32H750XBH6_APP`）里的 USB 虚拟串口（CDC）模块移植到其他
STM32 工程，以及移植后怎么使用。所有配置、代码片段、行号、编译产物数字均以本工程
当前代码为准。

- 实现文件：`Library/Src/usb_stdio.c`、`Library/Inc/usb_stdio.h`
- 结构特点：**所有业务逻辑都在 `Library/` 目录下，CubeMX 生成的代码里只留 2 行钩子**
  —— 这样 CubeMX 随便重新生成，代码都不会丢。
- 已在 **STM32H750XBH6 + USB_OTG_FS（PA11/PA12，HSI48 时钟）** 上验证通过：
  本工程 `main.c` 里 USART1 的 `printf` 与 USB 的 `usb_printf` 同时周期输出。

> ⚠️ 与 printf/scanf 重定向（USART1）的关系：本工程**两个模块并存** ——
> 标准 `printf`/`scanf` 走 USART1（见同目录 `串口printf重定向移植与使用.md`），
> `usb_printf`/`usb_scanf` 走 USB 虚拟串口。两者互不干扰，详见 [§6.5](#65-让标准-printfscanf-走-usb可选本工程现状说明)。

---

## 目录

1. [它到底是什么](#1-它到底是什么)
2. [CubeMX 配置](#2-cubemx-配置)
3. [移植步骤](#3-移植步骤)
4. [CMake 配置（默认没有 Library 目录）](#4-cmakelists-配置默认没有-library-目录)
5. [非 CMake 工程（Makefile / CubeIDE）](#5-非-cmake-工程makefile--cubeide)
6. [使用方法](#6-使用方法)
7. [API 参考](#7-api-参考)
8. [可调参数](#8-可调参数)
9. [设计要点与坑](#9-设计要点与坑)
10. [故障排查](#10-故障排查)
11. [验证方法](#11-验证方法)

---

## 1. 它到底是什么

一句话：**把格式化打印 / 接收的通道接到 USB 虚拟串口上，并保证不丢数据。**

数据流：

```
        ┌─────────────┐   CDC_Receive_FS()    ┌──────────────┐
USB OUT │  USB 中断    │ ────────────────────► │  环形缓冲     │
 ──────►│ (ISR 上下文) │  usb_stdio_rx_push()  │  1024 字节    │
        └─────────────┘                       └──────┬───────┘
                                                     │ usb_read() / usb_getchar()
                                                     │ usb_scanf()
                                                     ▼
                                                应用程序

        ┌─────────────┐
USB IN  │  USB 中断    │ ◄──────────────── usb_tx() ◄── usb_printf() / usb_write()
 ──────►│             │   CDC_Transmit_FS()              应用程序
        └─────────────┘
```

相比"直接调 `CDC_Transmit_FS` / 在回调里处理数据"的裸写法，这个模块解决了三件麻烦事：

| 问题 | 裸写法 | 本模块 |
|---|---|---|
| 发送 | 端点忙时返回 `USBD_BUSY`，数据**静默丢失** | 阻塞 + 指数退避重试，发完才返回 |
| 接收 | 回调里必须马上处理，否则数据被覆盖 | 中断只入环形缓冲，应用慢慢读 |
| 超长发送 | 一次性丢给 USB 栈，主机来不及收会丢 | 自动按 64 字节分块 + 块间留 1ms 间隔 |

---

## 2. CubeMX 配置

### 2.1 必须的配置项（本工程实际值）

| 配置位置 | 设置值 | 说明 |
|---|---|---|
| **Connectivity → USB_OTG_FS** | `Mode: Device_Only` | 只做设备 |
| **Middleware → USB_DEVICE** | `Class For FS IP: Communication Device Class (Virtual Port Com)` | CDC |
| **引脚** | `PA11 = USB_OTG_FS_DM`、`PA12 = USB_OTG_FS_DP`（AF10） | FS 差分对 |
| **Clock Configuration → USB Clock (USBCLK)** | **`HSI48`** | ⚠️ USB 必须精确 48MHz |
| **RCC → HSI48** | `ON`（`RCC_HSI48_ON`） | HSI48 被 USB 用，必须使能 |
| **NVIC → USB_OTG_FS global interrupt** | 使能，抢占优先级 **0** | 必须；见下方优先级说明 |
| **Project Manager → Toolchain/IDE** | `CMake` | 与本文档的构建章节对应 |
| **Code Generator** | 勾选 *Generate peripheral initialization as a pair of .c/.h files* | 与本文档目录结构一致 |

> **关于 48MHz 时钟源**：本工程用 **HSI48**（内部 48MHz RC），不占 PLL。
> 生成的代码在 `USB_DEVICE/Target/usbd_conf.c:79` 的 `HAL_PCD_MspInit()` 里：
>
> ```c
> PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
> PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
> HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
> ```
>
> 也可以改用 `RCC_USBCLKSOURCE_PLLQ` / PLL3 等，只要保证 **48MHz** 即可。

> **关于 USB 电压检测**：`HAL_PWREx_EnableUSBVoltageDetector()` 出现在两处，都是生成的：
> `usbd_conf.c` 的 `HAL_PCD_MspInit()` 和 `USB_DEVICE/App/usb_device.c:89`
> （`MX_USB_DEVICE_Init()` 末尾）。保留即可，不要删。

> **关于中断优先级（本工程实际值）**：`.ioc` 里
> `NVIC.OTG_FS_IRQn` 抢占优先级 **0**，`NVIC.SysTick_IRQn` 抢占优先级 **15**。
> 本模块的收发函数内部用 `HAL_Delay()` 等待（依赖 SysTick 中断），因此
> **USB 中断优先级必须高于（数值小于）SysTick**，否则等待循环会把 USB 中断饿死。
> 保持本工程的取值即可。

### 2.2 FIFO 分配（重要）

`USB_DEVICE/Target/usbd_conf.c:378-380`（CubeMX 生成的默认值）：

```c
HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);   /* RX FIFO  = 128 字节 */
HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x40); /* EP0 TX   =  64 字节 */
HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x80); /* EP1 TX   = 128 字节 (CDC IN) */
```

⚠️ **EP1 (CDC IN) 的 TX FIFO 大小决定了发送分块上限。** 本模块的
`USB_STDIO_TX_CHUNK_SIZE` 是 **64**，必须 **≤ EP1 FIFO 大小（128）**，否则
`USB_WritePacket()` 会溢出端点 FIFO 导致数据错乱。改了这里记得同步改宏。

### 2.3 本工程的 `.ioc` 摘要（供对照）

```ini
# STM32H750XBH6_APP.ioc 关键行
Mcu.IP7=USB_DEVICE
Mcu.IP8=USB_OTG_FS
PA11.Mode=Device_Only
PA11.Signal=USB_OTG_FS_DM
PA12.Mode=Device_Only
PA12.Signal=USB_OTG_FS_DP
USB_DEVICE.CLASS_NAME_FS=CDC
USB_DEVICE.VirtualMode-CDC_FS=Cdc
USB_OTG_FS.VirtualMode=Device_Only
RCC.USBCLockSelection=RCC_USBCLKSOURCE_HSI48
RCC.USBFreq_Value=48000000
RCC.SYSCLKFreq_VALUE=480000000     # HSE 25M, PLL 5/192/2
RCC.HCLKFreq_Value=240000000       # HPRE /2
NVIC.OTG_FS_IRQn=true:0:0:...      # 抢占优先级 0
NVIC.SysTick_IRQn=true:15:0:...    # 抢占优先级 15
```

USB 描述符（`USB_DEVICE/App/usbd_desc.c:65-68`）：VID = `1155`（0x0483，ST 官方），
PID = `22336`（0x5740，Virtual ComPort）。Windows 10/11 自带驱动，无需装驱动。

---

## 3. 移植步骤

### 步骤总览

```
① 建目录 + 拷 2 个文件     Library/Inc/usb_stdio.h
                          Library/Src/usb_stdio.c
                              ↓
② 加 2 行钩子             USB_DEVICE/App/usbd_cdc_if.c
                              ↓
③ 配置构建系统             Library/CMakeLists.txt   (新建)
                          顶层 CMakeLists.txt      (改 2 行)
                              ↓
④ 重新 configure + 编译    并验证符号链进来了
```

> 💡 **前提**：本节按"目标工程是 CubeMX 刚生成的 CMake 工程、**没有
> `Library/` 目录**"来写。构建配置细节见
> [§4 CMakeLists 配置](#4-cmakelists-配置默认没有-library-目录)。
> 如果目标工程已经有 `Library/`（比如已按本文档移植过 retarget 模块），
> 直接把 2 个文件丢进去即可，`file(GLOB)` 会自动收集，CMake 无需再改。

### ① 拷贝模块文件

把这两个文件原样拷到目标工程（目录名可以不同，保持一致即可）：

```
你的工程/
├── Library/
│   ├── Inc/usb_stdio.h     ← 拷贝
│   └── Src/usb_stdio.c     ← 拷贝
```

**`usb_stdio.c` 里唯一可能需要改的地方**在文件头部（第 35 行附近）：

```c
#include "usb_device.h"
#include "usbd_cdc_if.h"

/* USB 设备句柄定义在 usb_device.c，但没有任何头文件声明它，所以这里手动声明。
   如果你在 CubeMX 里改了它的名字，只需要改这一行。 */
extern USBD_HandleTypeDef hUsbDeviceFS;   /* ← 句柄名不是 hUsbDeviceFS 时改这里 */
```

### ② 在生成的 `usbd_cdc_if.c` 里加钩子

只改 **2 处**，都在 CubeMX 的 `USER CODE` 保护区内（重新生成不会丢）。

**a) 加 include**（`USB_DEVICE/App/usbd_cdc_if.c:24-26`）：

```c
/* USER CODE BEGIN INCLUDE */
#include "usb_stdio.h"          /* USB虚拟串口打印/接收的实际实现 */
/* USER CODE END INCLUDE */
```

**b) 转发接收到的数据**（`CDC_Receive_FS()`，本工程在 261 行附近）：

```c
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  /* Forward the received bytes to the USB virtual COM port RX ring buffer
     (Library/Src/usb_stdio.c), they are then read by usb_scanf()/usb_read(). */
  usb_stdio_rx_push(Buf, (Len != NULL) ? *Len : 0U);

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}
```

`usbd_cdc_if.h` **不需要任何改动**，保持 CubeMX 原样。

> 本工程 `usbd_cdc_if.c` 里没有其他用户改动：`CDC_Init_FS` / `CDC_Transmit_FS`
> 里的 `TxState` 检查等都是 CubeMX 模板自带的。收发缓冲大小
> `APP_RX_DATA_SIZE / APP_TX_DATA_SIZE = 2048`（`usbd_cdc_if.h:52-53`）保持默认即可。

### ③ 配置构建系统

- **CMake 工程** → 见 [§4](#4-cmakelists-配置默认没有-library-目录)（推荐）
- **Makefile / STM32CubeIDE** → 见 [§5](#5-非-cmake-工程makefile--cubeide)

### ④ 初始化与验证

`main.c` 里保证 **先 `MX_USB_DEVICE_Init()` 再调用收发函数**（本工程
`main.c:95-97` 的顺序就是对的），然后按 [§11 验证方法](#11-验证方法) 上电验证。

---

## 4. CMakeLists 配置（默认没有 Library 目录）

**本节假设你的工程是 CubeMX 刚生成的 CMake 工程，只有下面这些目录，
没有任何 `Library/`：**

```
你的工程/
├── CMakeLists.txt              ← CubeMX 生成（只生成一次，之后可自由改）
├── CMakePresets.json
├── cmake/
│   ├── gcc-arm-none-eabi.cmake ← 工具链文件
│   └── stm32cubemx/
│       └── CMakeLists.txt      ← CubeMX 每次重新生成，不要手改
├── Core/
│   ├── Inc/
│   └── Src/                    ← main.c, syscalls.c, sysmem.c, ...
├── Drivers/                    ← HAL + CMSIS
├── Middlewares/                ← USB Device Library
├── USB_DEVICE/
│   ├── App/                    ← usbd_cdc_if.c, usb_device.c, usbd_desc.c
│   └── Target/                 ← usbd_conf.c
└── startup_xxx.s
```

### 4.1 先理解 CubeMX 生成的 CMake 结构

看 `cmake/stm32cubemx/CMakeLists.txt` 的关键几行（本工程 90-112 行）：

```cmake
# ① INTERFACE 库: 只承载"头文件路径"和"宏定义", 不编译任何源文件
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# ② OBJECT 库: 承载 HAL 驱动源码
add_library(STM32_Drivers OBJECT)
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)

# ③ OBJECT 库: 承载 USB Device Library 源码
add_library(USB_Device_Library OBJECT)
target_link_libraries(USB_Device_Library PUBLIC stm32cubemx)

# ④ 应用层源码(含 usbd_cdc_if.c) 直接挂在主可执行目标上
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# ⑤ 主目标链接上面这些库
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})
```

| 目标 | 类型 | 作用 |
|---|---|---|
| `stm32cubemx` | **INTERFACE** | 所有 include 路径 + 宏（`USE_HAL_DRIVER` 等）都挂在它身上 |
| `STM32_Drivers` | OBJECT | HAL 驱动 |
| `USB_Device_Library` | OBJECT | USB 设备协议栈 |
| `${CMAKE_PROJECT_NAME}` | 可执行 | CubeMX 应用源码（含 `usbd_cdc_if.c`） |

所以：**`usbd_cdc_if.c` 能 include 到哪些头文件，取决于它所在目标拿到了哪些
include 路径。** 我们要把 `Library/Inc` 也加到这条链上。

### 4.2 创建目录并放入文件

在工程根目录新建（大小写按你喜好，本文档统一用 `Library`）：

```
你的工程/
└── Library/
    ├── CMakeLists.txt          ← 新建
    ├── Inc/
    │   └── usb_stdio.h         ← 从本工程拷贝
    └── Src/
        └── usb_stdio.c         ← 从本工程拷贝
```

PowerShell：

```powershell
New-Item -ItemType Directory -Force Library\Inc, Library\Src
Copy-Item <本工程>\Library\Inc\usb_stdio.h Library\Inc\
Copy-Item <本工程>\Library\Src\usb_stdio.c Library\Src\
```

### 4.3 新建 `Library/CMakeLists.txt`

**直接照抄本工程这一份**（与本工程 `Library/CMakeLists.txt` 逐字一致）：

```cmake
# ============================================================================
#  Library/CMakeLists.txt
#  用户代码库: 存放不属于 CubeMX 生成范围的自定义模块
#  (如 usb_stdio.c 的 USB 虚拟串口 printf/scanf)
#
#  注意: 本文件由用户维护, CubeMX 不会生成也不会覆盖它。
# ============================================================================

# 收集本目录下所有源文件
# 提示: file(GLOB) 是在"配置阶段"展开的, 新增 .c 文件后需要重新 configure
#       (改 CMakeLists.txt 也会触发, 所以一般不用手动管)
file(GLOB USER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c")

add_library(user_library STATIC ${USER_SOURCES})

# ---------------------------------------------------------------------------
# Inc 目录必须用 PUBLIC:
#   - usb_stdio.c 自己要用
#   - CubeMX 生成的 USB_DEVICE/App/usbd_cdc_if.c 要 include "usb_stdio.h"
#     而那个文件属于主可执行目标, 只有 PUBLIC/INTERFACE 才能传播过去
# ---------------------------------------------------------------------------
target_include_directories(user_library
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

# ---------------------------------------------------------------------------
# 链接 stm32cubemx, 并且必须是 PUBLIC:
#   - usb_stdio.c 里 include 了 "usb_device.h" / "usbd_cdc_if.h",
#     这些头文件的搜索路径挂在 stm32cubemx (INTERFACE 库) 上
#   - PUBLIC 让它继续传播给主目标, 于是 usbd_cdc_if.c 也能找到 usb_stdio.h
#   - 顺带把 USE_HAL_DRIVER 等宏也带过来了
# 用 PRIVATE 的话: usbd_cdc_if.c 会报 "usb_stdio.h: No such file or directory"
# ---------------------------------------------------------------------------
target_link_libraries(user_library
        PUBLIC
        stm32cubemx
)
```

### 4.4 改顶层 `CMakeLists.txt`（2 处）

CubeMX 生成的顶层 `CMakeLists.txt` 里有现成的占位注释，照着填即可。
本工程实际改后的样子（`CMakeLists.txt:48-52` 和 `78-83`）：

**① 在 `add_subdirectory(cmake/stm32cubemx)` 之后注册子目录**

```cmake
# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Add User Library                      ← CubeMX 预留的位置
add_subdirectory(Library)               ← 加这一行
```

**② 在 `target_link_libraries` 里链接它**

```cmake
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries         ← CubeMX 预留的位置
        user_library                     ← 加这一行
)
```

> `CMakeLists.txt` 和 `Library/CMakeLists.txt` 都是**用户维护**的文件，
> CubeMX **不会**覆盖它们（CubeMX 只重新生成 `cmake/stm32cubemx/CMakeLists.txt`
> 和各种 `*.c/.h`，而且只改 `USER CODE` 区外的内容）。

### 4.5 浮点格式化（%f）—— ⚠️ 本工程当前**未开启**

工具链用了 `--specs=nano.specs`（`cmake/gcc-arm-none-eabi.cmake:44`），newlib-nano
默认**不带浮点格式化**：

- `printf("%.5f", ...)` → **输出空字符串**，不报错
- `scanf("%f", ...)` → **匹配失败**，返回值少 1

症状极具迷惑性：串口上只有一堆空行，看起来像"没数据"。

**本工程当前没有加浮点参数**（已核对 `cmake/gcc-arm-none-eabi.cmake` 和链接产物
map 文件，`_printf_float` / `_scanf_float` 符号数为 0），所以**当前固件里 `%f`
不可用**。本工程现有代码只打印整数（`"tick %lu"`），不受影响。

需要用 `%f` 时，在 `cmake/gcc-arm-none-eabi.cmake` 末尾追加：

```cmake
# newlib-nano 默认不带浮点格式化, printf/scanf 的 %f 会静默失效
# (printf 只输出空字符串, scanf 直接匹配失败), 下面两个 -u 把浮点支持链进来。
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -u _printf_float -u _scanf_float")
```

> 💡 代价：参考工程实测约 **+15 KB** Flash（`55092 → 70136`）。本工程
> STM32H750 内部 Flash 只有 128 KB，当前全工程占用 50656 B（38.6%，见
> [§9.7](#97-flash-占用)），开浮点后预计升到 ~52%。只打印整数就不要开；
> 空间紧张时可以只开 `-u _printf_float`（省掉 scanf 浮点解析的 ~5 KB）。

> ⚠️ **改工具链文件后必须重新 configure！**
> CMake **不会**因为工具链文件被修改而自动重跑配置阶段，`build.ninja` 里的
> `LINK_FLAGS` 会保持旧值，表现为"改了完全没效果"。改完记得：
>
> ```bash
> cmake --preset Debug
> cmake --build build/Debug
> ```
>
> 验证参数真的进去了：
>
> ```bash
> grep LINK_FLAGS build/Debug/build.ninja | head -1
> # 应该能看到 -u _printf_float -u _scanf_float
> ```

### 4.6 编译验证

```bash
cmake --preset Debug          # 新增了子目录 / 改了工具链文件, 重新配置
cmake --build build/Debug
```

**验证 1：`usb_stdio.c` 真的被编译并链进来了吗**

```bash
# 看静态库成员
arm-none-eabi-ar t build/Debug/Library/libuser_library.a
# 期望: CMakeFiles/user_library.dir/Src/usb_stdio.c.obj

# 看最终 ELF 里的符号
arm-none-eabi-nm build/Debug/STM32H750XBH6_APP.elf | grep -E "usb_printf|usb_stdio_rx_push"
# 期望(地址可能不同):
#   0800xxxx T usb_printf
#   0800xxxx T usb_stdio_rx_push
```

**验证 2：浮点符号（仅当按 §4.5 开启后）**

```bash
arm-none-eabi-nm build/Debug/STM32H750XBH6_APP.elf | grep -E "_printf_float|_scanf_float"
# 期望: 两个 W 符号都能找到；找不到 = %f 不可用
```

**验证 3：烧进去看现象**

设备管理器应出现 `STMicroelectronics STM32 Virtual ComPort (COMx)`，
按 [§11](#11-验证方法) 用串口助手验证。

### 4.7 两个备选方案（不建 `Library/` 目录）

如果不想为了两个文件新增一个静态库，可以二选一：

**方案 B：放进 `Core/Src` + `Core/Inc`**

⚠️ 缺点：`Core/` 是 CubeMX 的地盘，`cmake/stm32cubemx/CMakeLists.txt`
**每次重新生成都会被覆盖**，往里手工加源文件下次生成就丢了。

**方案 C：头文件方式（只 `#include` 到某个 .c）**

最省事，但代码组织差，不推荐。

**推荐还是方案 A（`Library/` 子目录）**：CubeMX 完全不会碰它，结构清晰，
后续再加别的自定义模块（如 printf 重定向 `retarget.c`）也是往同一个目录丢。

---

## 5. 非 CMake 工程（Makefile / CubeIDE）

| 需要做的事 | Makefile | STM32CubeIDE |
|---|---|---|
| 编译 `usb_stdio.c` | 加进 `C_SOURCES` | 右键 *Refresh* 后手动加入源文件树 |
| 头文件路径 | `C_INCLUDES += -ILibrary/Inc` | *Project → Properties → C/C++ Build → Settings → Include paths*（`<All configurations>`） |
| 浮点 printf/scanf | `LDFLAGS += -u _printf_float -u _scanf_float` | *Linker → Misc → Other flags* |
| `CMSIS/Device` 等已有路径 | CubeMX 已生成，不用动 | 同左 |

⚠️ **CubeIDE 用户注意**

- 新建的 `Library/` 目录**不会自动纳入编译**，必须手动加入
- CubeIDE 用自己的 `.cproject` 配置，`Library/CMakeLists.txt` 那套对它无效
- 修改后记得 *Project → C/C++ Build → Clean*

---

## 6. 使用方法

### 6.1 最小例子（本工程 `main.c` 的实际写法）

```c
/* USER CODE BEGIN Includes */
#include "retarget.h"              // printf 重定向到 USART1 (与本模块并存)
#include "usb_stdio.h"
/* USER CODE END Includes */

int main(void)
{
    /* ... MPU/时钟/外设初始化 ... */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USB_DEVICE_Init();          /* 必须先初始化 USB */
    /* USER CODE BEGIN 2 */
    RetargetInit(&huart1);         // printf -> USART1
    /* USER CODE END 2 */

    while (1)
    {
        printf("\r\nHello!\r\n");                                    /* USART1 */
        usb_printf("tick %lu\r\n", (unsigned long) HAL_GetTick());   /* USB */
        HAL_Delay(500);
    }
}
```

> USB 枚举需要几百毫秒。上电后立刻 `usb_printf` 的头几条可能发不出去
> （`usb_tx_ready()` 会等 `USBD_STATE_CONFIGURED`，超时后丢弃）——这是正常
> 现象，等主机枚举完成后输出就稳定了。

### 6.2 接收：两种典型场景

**场景 A：定长 / 结构化短输入（如 `123 4.5`）**

```c
int i = 0;

if (usb_scanf("%d", &i) == 1)
{
    usb_printf("OK: int=%d\r\n", i);
}
```

⚠️ `%f` 需要先按 [§4.5](#45-浮点格式化f-️-本工程当前未开启) 开启浮点链接参数，当前工程未开启。

**场景 B：命令 + 变长数据（推荐用 `usb_read`）**

```c
char line[256];
uint32_t got;

got = usb_read((uint8_t *)line, sizeof(line) - 1U, 200U);   /* 等 200ms */
if (got > 0U)
{
    line[got] = '\0';
    /* ... 解析命令 ... */
}
```

### 6.3 非阻塞轮询（不卡主循环）

```c
if (usb_stdio_rx_available() > 0U)      /* 先问有没有数据 */
{
    /* 有数据再去读, 就不会阻塞 */
    uint32_t n = usb_read((uint8_t *)buf, sizeof(buf), 10U);
    /* ... */
}
```

### 6.4 长数据发送

模块内部已经做了分块和流控（`usb_tx()`，`Library/Src/usb_stdio.c:187-253`）：

- 按 `USB_STDIO_TX_CHUNK_SIZE`（64 字节）自动分块；
- 每块发送前等 IN 端点空闲（最长 `USB_STDIO_TX_TIMEOUT_MS` = 1000ms）；
- `CDC_Transmit_FS` 返回 `USBD_BUSY` 时按 1→2→4→8ms 指数退避重试；
- 每块发完 `HAL_Delay(1)`，给完成中断和主机留出时间。

所以 **`usb_write()` 一次发几百字节是安全的**（最坏阻塞时间约为
`块数 × 1000ms`，正常情况几十毫秒内完成）。

但要注意 **`usb_printf()` 是另一回事**：它先把格式化结果写进一个
**64 字节的栈上缓冲**（`usb_stdio.c:393`），**单次调用最多输出 63 个字符，
超出部分被截断**。打印长文本请直接用 `usb_write`：

```c
/* ❌ 超过 63 字符会被截断 */
usb_printf("%s\r\n", long_text);

/* ✅ 长文本用 usb_write */
usb_write((const uint8_t *)long_text, strlen(long_text));
usb_write((const uint8_t *)"\r\n", 2U);
```

### 6.5 让标准 `printf`/`scanf` 走 USB（可选，本工程现状说明）

`usb_stdio.c` 文件末尾（第 508 行起）预留了两个可选宏，但**在本工程的默认
状态下它们都没定义**，`usb_stdio.c` 不定义任何 `_write`/`_read`，因此与
USART1 重定向模块 `retarget.c` **完全无冲突地并存**（这正是当前工程的用法）。

如果确实想让标准 `printf` 走 USB，**先了解本工程符号覆盖的真实情况**：

| 做法 | 实际效果 |
|---|---|
| 定义 `USB_STDIO_RETARGET` | 定义 `__io_putchar/__io_getchar`。但本工程同时编译了 `retarget.c`，它的**强** `_write`/`_read` 覆盖了 `syscalls.c` 的弱版本，`__io_*` 钩子根本不会被调用 → **在本工程没有效果** |
| 定义 `USB_STDIO_RETARGET_FORCE` | `usb_stdio.c` 也定义**强** `_write`/`_read`，与 `retarget.c` 的强符号冲突（两者都在 `libuser_library.a` 里）→ **链接报 multiple definition 错误** |

**本工程推荐的正确做法**（如果真要让 printf 走 USB）：

1. 把 `Library/Src/retarget.c` 移出编译（改名、删除，或在
   `Library/CMakeLists.txt` 的 `file(GLOB)` 后 `list(REMOVE_ITEM)` 掉）；
2. 编译 `usb_stdio.c` 时定义 `USB_STDIO_RETARGET`（走 `syscalls.c` 的弱
   `_write` → `__io_putchar` → USB，无符号冲突）；
3. 删掉 `main.c` 里的 `RetargetInit(&huart1)` 调用。

> 不过一般没必要：**并存使用 `printf`（USART1）+ `usb_printf`（USB）**
> 是更简单、更常用的方案，一路接调试、一路接上位机。

---

## 7. API 参考

### 发送

| 函数 | 说明 |
|---|---|
| `int usb_printf(const char *fmt, ...)` | 格式化输出，阻塞到发完。**内部 64 字节缓冲，单次最多 63 字符，超长截断** |
| `int usb_vprintf(const char *fmt, va_list ap)` | va_list 版本，给日志封装用 |
| `uint32_t usb_write(const uint8_t *buf, uint32_t len)` | 原始数据发送，自动分块+重试。返回**实际发出**的字节数，< `len` 说明超时截断 |
| `int usb_putchar(int ch)` | 单字节，返回写入的字符或 -1 |

### 接收

| 函数 | 说明 |
|---|---|
| `uint32_t usb_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)` | 阻塞读。`timeout_ms` 是**首字节**等待时间；首字节到达后把缓冲里已有的全部读走。返回实际字节数 |
| `int usb_getchar(void)` | 单字节，超时（3s）返回 -1 |
| `int usb_scanf(const char *fmt, ...)` | 见下方限制 |
| `int usb_vscanf(const char *fmt, va_list ap)` | va_list 版本 |

### 环形缓冲管理

| 函数 | 说明 |
|---|---|
| `uint32_t usb_stdio_rx_available(void)` | 当前缓冲里的字节数，用于非阻塞判断 |
| `void usb_stdio_rx_flush(void)` | 丢弃缓冲里所有数据（**不要在读完命令后立刻调用**，见 [§9.4](#94-不要在读完命令后立刻-flush-缓冲)） |
| `uint32_t usb_stdio_rx_overrun(void)` | 累计因缓冲满而丢弃的字节数，用于监控 |
| `void usb_stdio_rx_push(const uint8_t *buf, uint32_t len)` | 供 `CDC_Receive_FS` 调用，**应用层不要用** |

### ⚠️ `usb_scanf` 的重要限制

**它按"一行一次"工作，不是真正的 scanf。**

- 收到第一个字节后，持续收集直到**行空闲超过 `USB_STDIO_LINE_GAP_MS`（20ms）**，
  把整行交给 `vsscanf()`，然后**丢弃 vsscanf 没消费掉的部分**。
- 后果 1：**同一行的后续 token 会丢失**，第二次调用接不上第一次。
- 后果 2：**"命令 + 变长数据"不能用它解析**（用 `usb_read`）。
- 后果 3：如果主机发送在**行中间**停顿超过 20ms，行会被提前截断。

**建议**：只用于短的、自包含的输入；不确定就用 `usb_read` + `sscanf`。

---

## 8. 可调参数

全部在 `Library/Inc/usb_stdio.h`，用 `#ifndef` 包着，可以在编译时用
`-DXXX=yyy` 覆盖：

| 宏 | 默认 | 说明 |
|---|---|---|
| `USB_STDIO_RX_RING_SIZE` | `1024` | 接收环形缓冲大小，**必须是 2 的幂**（有 `#error` 编译期检查） |
| `USB_STDIO_TX_CHUNK_SIZE` | `64` | 发送分块大小。编译期检查范围 1..255；**同时必须 ≤ EP1 TX FIFO**（本工程 0x80=128，见 §2.2） |
| `USB_STDIO_TX_TIMEOUT_MS` | `1000` | 每个分块等 IN 端点空闲的超时 |
| `USB_STDIO_RX_TIMEOUT_MS` | `3000` | `usb_getchar` / `usb_scanf` 等首字节的超时 |
| `USB_STDIO_LINE_GAP_MS` | `20` | `usb_scanf` 判断行尾的空闲间隔 |
| `USB_STDIO_SCAN_LINE_SIZE` | `256` | `usb_scanf` 的行缓冲（**栈上**，注意栈深；有 `#error` 要求 ≥ 2；超长行被静默截断） |

> ⚠️ `USB_STDIO_TX_CHUNK_SIZE ≤ EP1 TX FIFO` 这条**没有编译期检查**，
> 因为它取决于 `usbd_conf.c` 里的 `HAL_PCDEx_SetTxFiFo(..., 1, 0x??)`，
> 编译器看不到。改 FIFO 时请自己确认。

**调参建议**：

- 接收高速数据流 → 调大 `USB_STDIO_RX_RING_SIZE`（如 4096），并且**及时读走**，
  用 `usb_stdio_rx_overrun()` 监控是否溢出
- 发送长数据 → 保持 `TX_CHUNK_SIZE` 不变（受 FIFO 限制），注意 `usb_write`
  最坏阻塞 `(len/chunk) × TX_TIMEOUT_MS`
- 栈紧张 → 调小 `USB_STDIO_SCAN_LINE_SIZE`（它是 `usb_vscanf` 的局部数组）
- 主机发数据被 `usb_scanf` 截断 → 适当调大 `USB_STDIO_LINE_GAP_MS`

---

## 9. 设计要点与坑

这一节建议全部读一遍，都是围绕本工程代码的真实行为。

### 9.1 `%f` 在当前工程会静默失效 ⭐

本工程**未开启**浮点链接参数（见 [§4.5](#45-浮点格式化f-️-本工程当前未开启)）：

- `usb_printf("%.5f")` → 输出空字符串，不报错
- `usb_scanf("%f")` → 匹配失败，返回值少 1

**症状极具迷惑性**：串口上只有空行/丢项。要么按 §4.5 开浮点，要么避开 `%f`
（用 `%d` 传整数、放大 1000 倍再除）。

### 9.2 端点忙 (USBD_BUSY) 必须重试 ⭐

`CDC_Transmit_FS()` 在 `TxState != 0` 时返回 `USBD_BUSY`。**不能当成硬错误**。
本模块的处理在 `usb_tx()`（`usb_stdio.c:219-247`）：1→2→4→8ms 指数退避重试，
直到成功或超时。

**为什么"检查了 `TxState == 0` 还会 busy"**：`usb_tx_ready()` 检查通过后，
到真正调用 `CDC_Transmit_FS()` 之间，`TxState` 可能被上一次传输的完成中断
**重新置回 1**。这个竞态窗口是真实存在的，所以重试是必需的。

### 9.3 发送缓冲独立、块间有间隔

`usb_write` 内部用自己的 `usb_tx_buffer`（64 字节静态数组），调用者传进来的
缓冲可以立刻复用。每个分块发完有 `HAL_Delay(1)`，让完成中断先跑、主机先收，
避免背靠背 IN 传输把主机接收缓冲压垮。

反过来的提醒：**`CDC_Transmit_FS()` 本身是异步的**，如果你绕过本模块直接调它，
调用后不能立刻修改传给它的缓冲区。

### 9.4 不要在读完命令后立刻 flush 缓冲

```c
/* ❌ usb_scanf 已经把整行收进它自己的栈缓冲并丢弃了未消费部分,
      flush 之后再想读"载荷"就永远读到 0 */
usb_scanf("%s", cmd);
usb_stdio_rx_flush();
usb_read(payload, ...);        /* 返回 0 ! */

/* ✅ 命令+载荷协议改用 usb_read 整体读入, 再自己拆分解析 */
```

### 9.5 不要在中断里调用这些函数

所有收发函数都是**阻塞**的（内部有 `HAL_Delay`）。如果在**优先级高于 USB 中断**
的 ISR 里调用，USB 中断无法执行 → 必然超时。

- 只在 main 循环 / 低优先级任务里调用
- 本工程 USB 中断优先级是 0（最高组），绝大多数中断都低于它，但**未来新增
  高优先级中断时要注意**
- 如果必须在 ISR 里输出，改成"ISR 只置标志 / 入队，主循环发送"

### 9.6 环形缓冲是"无锁单生产者单消费者"设计

`usb_rx_head` 只由 USB 中断写，`usb_rx_tail` 只由应用写 —— 所以不需要临界区。
如果你改了代码让两边都写同一个变量，就必须加临界区保护。

### 9.7 Flash 占用

本工程当前实测（Debug 构建，**未开浮点**）：

```
text 50304 + data 352 = 50656 B Flash / 128 KB = 38.6%
bss 10304 B（含 USB 收发缓冲 2×2048、环形缓冲 1024 等）
```

> STM32H750 内部 Flash **只有 128 KB**。开启 `-u _printf_float -u _scanf_float`
> 参考实测约 **+15 KB**（55 KB 工程 → 70 KB）。空间紧张时优先考虑只开
> `_printf_float`，或干脆不打印浮点。

### 9.8 与 USART1 printf 重定向并存（本工程现状）

- `printf` → `retarget.c` 的强 `_write` → `HAL_UART_Transmit(&huart1)`
- `usb_printf` → `usb_tx` → `CDC_Transmit_FS`
- `usb_stdio.c` 默认不定义任何 `_write`/`_read`，两者零冲突
- 验证：`arm-none-eabi-nm build/Debug/STM32H750XBH6_APP.elf | grep " T _write"`
  应指向 `retarget.c.obj`（本工程实测 `0x0800ae4c _write`，来自
  `libuser_library.a(retarget.c.obj)`；`syscalls.c` 的弱版本被丢弃，地址为 0）

---

## 10. 故障排查

### 设备管理器里没有出现 COM 口

| 检查项 | 说明 |
|---|---|
| USB 线插对口了吗 | 必须是 **OTG_FS** 口（PA11/PA12），不是板载 USB-TTL（那是 USART1） |
| USB 时钟是 48MHz 吗 | `.ioc` 里 `RCC.USBCLockSelection=RCC_USBCLKSOURCE_HSI48`；HSI48 要在 RCC 里开（`SystemClock_Config` 里有 `RCC_HSI48_ON`） |
| 有没有"未知 USB 设备" | 有 → 枚举失败，优先怀疑时钟和 PA11/PA12 接线 |
| `HAL_PWREx_EnableUSBVoltageDetector()` 调了吗 | 生成的代码里有两处，别删 |
| OTG_FS 中断使能了吗 | `.ioc` NVIC 里 `OTG_FS_IRQn` 必须勾选（本工程优先级 0） |
| 固件真的烧进去了吗 | 本工程链接脚本 `STM32H750xx_FLASH.ld` 的 FLASH 起始 `0x08000000`，烧录地址要一致；如果改跑外部 QSPI Flash（如 bootloader 方案），`ORIGIN` 和烧录地址要同步改 |

### 有 COM 口但收不到数据

- 等待枚举完成（上电后几百毫秒），`tick` 输出稳定后再判断
- 确认没有别的程序占用该 COM 口（串口助手、上位机只能开一个）
- 板载 USB-TTL（USART1）和 USB 虚拟串口是**两个不同的 COM 口**，别选错

### 收到一堆空行 / `%f` 打印为空

**浮点未开启** —— 见 [§9.1](#91-f-在当前工程会静默失效-)，按 §4.5 添加链接参数并重新 configure。

### 数据被截断在 63 字节附近

`usb_printf` 的 64 字节栈缓冲截断 —— 见 [§6.4](#64-长数据发送)，改用 `usb_write`。

### `usb_read`/`usb_printf` 长时间不返回

最坏阻塞时间 = 块数 × 1000ms（发送）或 3000ms（等首字节）。USB 线拔掉后
`usb_tx_ready()` 检测到 `dev_state != CONFIGURED` 会立刻返回 0（丢弃本次数据），
不会死等。

### `usb_scanf` 解析失败 / 只匹配到一半

[§7 的 `usb_scanf` 限制](#-usb_scanf-的重要限制) —— 改用 `usb_read` + `sscanf`，
或调大 `USB_STDIO_LINE_GAP_MS`。

---

## 11. 验证方法

本工程不带测试脚本，用任意串口助手即可完成验证：

**步骤 1 —— 认端口**

设备管理器 → "端口 (COM 和 LPT)"，插入 USB 线后应出现
`STMicroelectronics STM32 Virtual ComPort (COMx)`。记下 COM 号。
（板载 USB-TTL 转串口是另一个 COM 口，对应 USART1 的 `printf`，别混淆。）

**步骤 2 —— 看周期输出**

用串口助手打开该 COM 口，波特率随便填（如 115200）—— **CDC 虚拟串口的
波特率是摆设，不影响实际速率**。应看到每 500ms 一条：

```
tick 1500
tick 2000
tick 2500
```

同时 USART1 口上应看到 `Hello!`（验证两个通道并存互不干扰）。

**步骤 3 —— 测接收**

在 `main.c` 的 while 循环里临时加（或自行实现协议）：

```c
int v = 0;
if (usb_scanf("%d", &v) == 1)
{
    usb_printf("RX OK: %d\r\n", v);
}
```

串口助手发送 `123`（**勾选"发送新行"**，否则没有行结尾），应收到
`RX OK: 123`。

**步骤 4 ——（可选）长数据回环**

自行实现 `ECHO` 命令：收到 `ECHO` 后用 `usb_read` 收载荷 N 字节、再用
`usb_write` 原样发回，在串口助手/自写脚本里比对字节数与内容是否一致，
验证分块发送不丢数据。

---

## 附：文件清单

| 文件 | 性质 | 说明 |
|---|---|---|
| `Library/Inc/usb_stdio.h` | **用户代码** | 接口 + 可调参数，可自由修改 |
| `Library/Src/usb_stdio.c` | **用户代码** | 全部实现，可自由修改 |
| `USB_DEVICE/App/usbd_cdc_if.c` | CubeMX 生成 | 只改了 2 处 `USER CODE` 钩子（include + `CDC_Receive_FS` 转发） |
| `USB_DEVICE/App/usbd_cdc_if.h` | CubeMX 生成 | **未改动** |
| `USB_DEVICE/Target/usbd_conf.c` | CubeMX 生成 | FIFO 分配在 378-380 行，HSI48 时钟选择在 79 行 |
| `USB_DEVICE/App/usb_device.c` | CubeMX 生成 | `MX_USB_DEVICE_Init()` 初始化序列 + USB 电压检测 |
| `cmake/gcc-arm-none-eabi.cmake` | 工具链 | `--specs=nano.specs`；**浮点 `-u` 参数当前未加**（需要时见 §4.5） |
| `Library/CMakeLists.txt` | 构建 | `PUBLIC` 传播 include 路径，`file(GLOB)` 自动收集 |
| `CMakeLists.txt`（顶层） | 构建 | 2 处改动：`add_subdirectory(Library)` + 链接 `user_library` |

**核心原则**：业务代码集中在 `Library/`，CubeMX 生成的文件里只留
`USER CODE` 保护区内的 2 行钩子 —— 可以随时用 CubeMX 重新生成而不丢代码。
