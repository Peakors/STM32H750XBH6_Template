# 串口 printf/scanf 重定向（USART1）—— 移植与使用

本文档介绍如何把本工程（`STM32H750XBH6_APP`）的 **标准 `printf` / `scanf`
重定向到 USART1** 的模块移植到其他 STM32 工程，以及使用时必须知道的行为限制。
所有配置、代码片段、行号均以本工程当前代码为准。

- 实现文件：`Library/Src/retarget.c`、`Library/Inc/retarget.h`
- 结构特点：**重定向逻辑全部在 `Library/` 目录下，`Core/` 里只有 2 行用户代码**
  （一个 `#include`，一个 `RetargetInit()` 调用）—— CubeMX 随便重新生成都不丢。
- 已在本工程验证：`main.c` 主循环里 `printf("\r\nHello!\r\n")` 每 500ms 从
  USART1（PA9/PA10，115200-8N1）输出一次，与 USB 虚拟串口的 `usb_printf` 并存。

> 与 USB 虚拟串口的关系：本工程**两个打印通道并存** ——
> `printf`/`scanf` 走 USART1（本文档），`usb_printf`/`usb_scanf` 走 USB
> （见同目录 `USB虚拟串口移植与使用.md`）。两套模块零冲突，详见
> [§6.8](#68-与-usb-虚拟串口模块并存)。

---

## 目录

1. [它到底是什么](#1-它到底是什么)
2. [原理：GCC 下 printf 是怎么走到串口的](#2-原理gcc-下-printf-是怎么走到串口的)
3. [本工程 USART1 的 CubeMX 配置](#3-本工程-usart1-的-cubemx-配置)
4. [移植步骤](#4-移植步骤)
5. [使用方法](#5-使用方法)
6. [行为特性与限制（必读）](#6-行为特性与限制必读)
7. [故障排查](#7-故障排查)
8. [文件清单](#8-文件清单)

---

## 1. 它到底是什么

一句话：**把 C 标准库的 `printf` / `scanf` 收发通道接到 USART1 上**，
并且不引入任何中断、DMA、环形缓冲 —— 就是最朴素的阻塞收发。

数据流：

```
                 标准库调用链                          硬件
 printf("...") ─► newlib _write_r ─► _write()  ─► HAL_UART_Transmit(&huart1)
                 (retarget.c:33,     阻塞, 等待    发送完成才返回
                  强符号, 覆盖        发完)
                  syscalls.c 弱符号)

 scanf("%d",&v) ─► newlib _read_r ─► _read()   ─► HAL_UART_Receive(&huart1)
                 (retarget.c:64,     每次只收      无超时阻塞等待
                  强符号)            1 个字节      (HAL_MAX_DELAY)
```

整个模块没有额外缓冲、没有中断服务函数：**发多少等多少，收一个等一个**。
简单、可靠、够用，这是它的定位；代价见 [§6](#6-行为特性与限制必读)。

---

## 2. 原理：GCC 下 printf 是怎么走到串口的

### 2.1 GCC (newlib) 与 Keil (ARMCC) 的区别

| 工具链 | printf 最终调用的钩子 | scanf 最终调用的钩子 |
|---|---|---|
| Keil / ARMCC（AC5/AC6） | `fputc()` | `fgetc()` |
| **GCC（newlib / newlib-nano）** | **`_write()`** | **`_read()`** |

网上大量教程写 `fputc` 重定向，那是 Keil 的路子。**本工程用
arm-none-eabi-gcc，重定向必须实现 `_write`/`_read`**，`fputc` 写了也没人调。

### 2.2 弱符号覆盖机制（为什么不会跟 syscalls.c 冲突）

CubeMX 生成的 `Core/Src/syscalls.c` 里有一对**弱符号**实现：

```c
/* Core/Src/syscalls.c:35-36 */
extern int __io_putchar(int ch) __attribute__((weak));
extern int __io_getchar(void) __attribute__((weak));

/* Core/Src/syscalls.c:67, 80 —— 弱符号 _read/_write, 逐字符调 __io_* */
__attribute__((weak)) int _read(int file, char *ptr, int len)  { ... __io_getchar() ... }
__attribute__((weak)) int _write(int file, char *ptr, int len) { ... __io_putchar() ... }
```

而 `Library/Src/retarget.c` 提供了**强符号**的同名函数（`_write`/`_read`/
`_isatty`/`_close`/`_lseek`/`_fstat`）。GNU 链接器规则：**强符号直接覆盖弱
符号，不报任何冲突**。

本工程链接产物已验证（`build/Debug/STM32H750XBH6_APP.map`）：

```
.text._write   0x0800ae4c  0x58  Library/libuser_library.a(retarget.c.obj)   ← 生效
.text._write   0x00000000  0x38  .../Core/Src/syscalls.c.obj                 ← 被覆盖, 丢弃
```

所以 printf 的完整调用链是：

```
printf ─► newlib 内部 _write_r ─► retarget.c 的 _write ─► HAL_UART_Transmit ─► PA9
```

### 2.3 `RetargetInit` 做的两件事

```c
/* Library/Src/retarget.c:17-23 */
void RetargetInit(UART_HandleTypeDef *huart) {
    gHuart = huart;                        /* 记住目标串口句柄 */
    setvbuf(stdout, NULL, _IONBF, 0);      /* stdout 改为无缓冲 */
}
```

- `gHuart` 是模块内全局指针，`_write`/`_read` 都通过它访问串口 ——
  **传哪个串口的句柄，printf 就走哪个串口**（`&huart1`、`&huart3` 都行）。
- 关闭 stdout 缓冲后，`printf` 一返回，字符就已经（或正在）送上 TX 线，
  不会滞留在库缓冲里。

---

## 3. 本工程 USART1 的 CubeMX 配置

移植前先确认目标工程的串口配置。本工程实际值（`Core/Src/usart.c:41-69`）：

| 配置项 | 值 | 说明 |
|---|---|---|
| 外设 / 模式 | USART1，`Asynchronous` | 异步模式 |
| **波特率** | **115200** | 上位机串口助手必须一致 |
| 字长 / 校验 / 停止位 | 8 Bits / None / 1 | 即 115200-8-N-1 |
| 引脚 | **PA9 = USART1_TX，PA10 = USART1_RX**（AF7） | 接 USB 转串口模块要**交叉**：板 TX→模块 RX，板 RX→模块 TX |
| **FIFO Mode** | **Disabled** | 行为接近传统串口，配合阻塞收发 |
| 时钟源 | `RCC_USART16CLKSOURCE_D2PCLK2`（120 MHz） | 生成的 `HAL_UART_MspInit()` 里 |
| GPIO 速度 | `GPIO_SPEED_FREQ_LOW` | 115200 不需要高速档 |
| **NVIC：USART1 中断** | **不使能** | ⚠️ 本模块用阻塞 API，不需要中断 |

> 💡 **不需要开中断**是这套实现的优点之一：`HAL_UART_Transmit` /
> `HAL_UART_Receive` 是轮询状态寄存器的阻塞函数，`Core/Src/stm32h7xx_it.c`
> 里没有 `USART1_IRQHandler`，少一个中断要管。
> 代价是收数据时会原地等待（见 [§6.1](#61-阻塞且无超时)）。

---

## 4. 移植步骤

### 步骤总览

```
① CubeMX 里配好 USART1（Asynchronous, 115200-8N1）
        ↓
② 拷 2 个文件到 Library/     Library/Inc/retarget.h
                             Library/Src/retarget.c
        ↓
③ main.c 加 2 行（都在 USER CODE 区）
        ↓
④ 配置构建系统（目标工程已有 Library/ 则跳过）
        ↓
⑤ 编译 + 验证 _write 归属
```

### ① 拷贝模块文件

```
你的工程/
├── Library/
│   ├── Inc/retarget.h       ← 拷贝
│   └── Src/retarget.c       ← 拷贝
```

两个文件**不需要任何修改**：

- `retarget.h` include 了 `stm32h7xx_hal.h`（换系列芯片时改成本系列的 HAL 头即可）；
- `retarget.c` 不含任何芯片特定的东西，全部通过 `gHuart` 句柄操作串口。

> 如果目标工程已经按《USB虚拟串口移植与使用》建好了 `Library/` 目录
> （例如已移植 `usb_stdio.c`），**直接把这两个文件丢进去就行**：
> `Library/CMakeLists.txt` 的 `file(GLOB Src/*.c)` 会自动收集，无需改 CMake。

### ② 在 `main.c` 里加 2 行（都在 USER CODE 保护区）

**a) 加 include**（本工程 `Core/Src/main.c:26-29`）：

```c
/* USER CODE BEGIN Includes */
#include "retarget.h"   //重定向printf
/* USER CODE END Includes */
```

**b) 初始化**（本工程 `Core/Src/main.c:98-100`，紧跟在 `MX_USART1_UART_Init()`
之后）：

```c
    /* USER CODE BEGIN 2 */
    RetargetInit(&huart1); //重定向printf
    /* USER CODE END 2 */
```

> ⚠️ **`RetargetInit()` 必须在 `MX_USART1_UART_Init()` 之后调用**，
> 否则 `gHuart` 指向的串口还没初始化，第一次 printf 就卡死在
> `HAL_UART_Transmit` 里。

### ③ 配置构建系统（没有 `Library/` 目录时）

与《USB虚拟串口移植与使用》§4 完全相同，这里只列清单：

1. 新建 `Library/CMakeLists.txt`（`file(GLOB)` 收集 `Src/*.c` →
   `user_library` 静态库；include 目录用 **PUBLIC**；链接 `stm32cubemx`
   也用 **PUBLIC**）；
2. 顶层 `CMakeLists.txt` 加 `add_subdirectory(Library)` 和
   `target_link_libraries(... user_library)`；
3. 重新 configure + build。

完整文件内容照抄 `USB虚拟串口移植与使用.md` §4.3 / §4.4 即可。

### ④ 编译验证

```bash
cmake --preset Debug
cmake --build build/Debug
```

**验证 `_write` 确实来自 retarget.c（而不是 syscalls.c）**：

```bash
arm-none-eabi-nm build/Debug/STM32H750XBH6_APP.elf | grep " _write\| _read"
# 期望: 两个 T 符号, 地址非 0

# 进一步看归属（map 文件里应指向 retarget.c.obj）:
grep "_write " build/Debug/STM32H750XBH6_APP.map
# 期望: Library/libuser_library.a(retarget.c.obj)
```

**烧录看现象**：USB 转串口模块接 PA9/PA10（交叉），串口助手 115200-8N1
打开，复位后应看到周期性的 `Hello!`。

---

## 5. 使用方法

### 5.1 本工程的实际用法

```c
/* main.c 已有的 2 行之后, 主循环里直接用标准库函数: */
while (1) {
    printf("\r\nHello!\r\n");
    HAL_Delay(500);
}
```

### 5.2 所有走标准库的输出/输入函数都能用

重定向挂在 newlib 的 `_write`/`_read` 上，所以**整个 stdio 家族**都通了：

```c
printf("value = %d, 0x%08X\r\n", v, (unsigned)reg);   /* 格式化输出 */
puts("boot ok");                                       /* 自带 \n */
putchar('A');                                         /* 单字符 */
fprintf(stdout, "...");                               /* 显式 stdout */
scanf("%d %s", &num, buf);                            /* 格式化输入 */
getchar();                                            /* 单字符输入 */
fgets(line, sizeof(line), stdin);                     /* 读一行 */
```

### 5.3 scanf 的典型用法

```c
int value = 0;

printf("input a number:\r\n");
if (scanf("%d", &value) == 1)
{
    printf("got %d\r\n", value);
}
```

**上位机（串口助手）设置**：

| 设置 | 值 | 原因 |
|---|---|---|
| 波特率等 | 115200-8N1 | 与固件一致 |
| **本地回显 (Local Echo)** | **开** | 固件不回显你输入的字符，不开的话你打字看不见（见 [§6.2](#62-没有回显)） |
| **发送新行** | **勾选**（加 `\r\n`） | 让 scanf 能感知输入结束 |

> ⚠️ `%f`（浮点）当前**不可用**：本工程未开启
> `-u _printf_float -u _scanf_float`，`printf("%f")` 输出空串、
> `scanf("%f")` 匹配失败。开启方法见
> 《USB虚拟串口移植与使用》§4.5（两个模块共用同一个链接配置）。

---

## 6. 行为特性与限制（必读）

这套实现**故意做得简单**，下面每一条都是使用前要知道的。

### 6.1 阻塞且无超时

`_write` 用 `HAL_UART_Transmit(..., HAL_MAX_DELAY)`，`_read` 用
`HAL_UART_Receive(..., 1, HAL_MAX_DELAY)`（`retarget.c:37, 68`）：

| 方向 | 行为 | 实际影响 |
|---|---|---|
| 发送 | 等所有字节发完才返回 | 115200 下约 87 µs/字节，`printf` 长文本会占住 CPU 几毫秒，一般无感 |
| **接收** | **死等 1 个字节，永不超时** | **`scanf`/`getchar` 没等到输入就永远卡在那** |

所以：

- **不要**在需要实时响应的循环里无条件调 `scanf`；
- 需要"有数据才读"的场景，参考 `usb_stdio` 的做法（先查 `RXNE` 标志 /
  环形缓冲 + 超时），或者把 USART1 改成中断 + 环形缓冲方案（本模块未提供）。

### 6.2 没有回显

固件收到字符后**不会发回去**。串口助手如果不开"本地回显"，你打的字
屏幕上什么都不显示，但 scanf 其实收到了 —— 这是被问得最多的问题。

### 6.3 `_read` 每次只收 1 个字节

`retarget.c:68`：`HAL_UART_Receive(gHuart, (uint8_t *)ptr, 1, HAL_MAX_DELAY)`。
newlib 会为凑齐一个 token 反复调用它，逐字符读取。效率不高（每次调用都有
HAL 层开销），但 115200 交互输入完全够用。副作用是**无法批量读取原始字节流** ——
`fread(stdin)` 也只会 1 字节 1 字节地来。

### 6.4 `%f` 浮点当前不可用

见 [§5.3](#53-scanf-的典型用法) 的警告。开浮点的代价约 +15 KB Flash
（H750 内部 Flash 只有 128 KB，当前工程占用 50656 B / 38.6%）。

### 6.5 不要在中断里 printf

- `HAL_UART_Transmit` 是阻塞的，在 ISR 里调用会拖住中断；
- 若 printf 恰好又在主循环里执行到一半，`gHuart` 的 HAL 状态会被搅乱。
- 需要在中断里报消息 → 置标志 / 写队列，主循环里再 printf。

### 6.6 同一时刻只绑定一个串口

模块只有一个全局 `gHuart`。可以运行中切换：

```c
RetargetInit(&huart1);      /* printf 暂时走 USART1 */
/* ... */
RetargetInit(&huart3);      /* 之后 printf 改走 USART3 */
```

但**不能**两个串口同时当 stdout。要双通道同时打印，就是本工程的方案：
USART1 用 `printf`，USB 虚拟串口用 `usb_printf`。

### 6.7 切换目标串口只要改一个参数

`RetargetInit(&huartX)` 传哪个句柄就走哪个串口，`retarget.c` 本身零修改。
前提是该串口已由 CubeMX 初始化（`MX_USARTX_UART_Init()` 已调用）。

### 6.8 与 USB 虚拟串口模块并存

本工程 `Library/Src/` 下同时有 `retarget.c` 和 `usb_stdio.c`：

| 通道 | 函数 | 走向 |
|---|---|---|
| 标准 | `printf` / `scanf` | retarget.c 强 `_write`/`_read` → USART1 |
| USB 专用 | `usb_printf` / `usb_scanf` / `usb_read` / `usb_write` | CDC 端点 → USB 虚拟串口 |

`usb_stdio.c` 默认**不定义**任何 `_write`/`_read`（相关代码在
`#if defined(USB_STDIO_RETARGET) || defined(USB_STDIO_RETARGET_FORCE)`
保护区内，默认关闭），所以**两者零冲突**。若想让标准 printf 改走 USB，
见《USB虚拟串口移植与使用》§6.5 —— 注意在那个场景下 `retarget.c` 要移出编译。

---

## 7. 故障排查

### printf 没有任何输出

| 检查项 | 说明 |
|---|---|
| 接线交叉了吗 | 板子 **PA9 (TX) → 模块 RX**，**PA10 (RX) → 模块 TX**，共地 |
| 波特率 | 固件 115200，串口助手必须一致 |
| `RetargetInit(&huart1)` 调了吗 | 在 `MX_USART1_UART_Init()` **之后**调用 |
| `#include "retarget.h"` 加了吗 | 加在 main.c 的 `USER CODE BEGIN Includes` 区 |
| CMake 收到文件了吗 | `arm-none-eabi-ar t build/Debug/Library/libuser_library.a` 应列出 `retarget.c.obj` |

### 输出乱码

- 波特率不一致（最常见）；
- 时钟配置变了：本工程 HSE 25 MHz → PLL 480 MHz，`SystemClock_Config` 改过
  的话波特率会跟着错。核对 `.ioc`：`RCC.SYSCLKFreq_VALUE=480000000`、
  `RCC.USART16Freq_Value=120000000`。

### scanf / getchar 卡住不动

**这是设计行为**：无超时阻塞等输入（[§6.1](#61-阻塞且无超时)）。确认：

- 串口助手连的是 USART1 那个 COM 口（不是 USB 虚拟串口）；
- 发送时**勾了"发送新行"**（补 `\r\n`）；
- 终端开了本地回显后，看打字是否有显示（有回显说明终端在发，问题在固件侧
  接线/串口号；没回显先去终端设置里开）。

### `printf("%.3f")` 输出空 / `scanf("%f")` 失败

浮点未链接，见 [§6.4](#64-f-浮点当前不可用)。

### 想确认 printf 到底走了哪条路

```bash
grep "\.text\._write " build/Debug/STM32H750XBH6_APP.map
# 指向 retarget.c.obj  → 走 USART1 (本工程状态)
# 指向 syscalls.c.obj  → 重定向没生效, 检查 Library 是否真的编进去了
```

### 复位后第一条 printf 丢字

`MX_USART1_UART_Init()` 到第一条 printf 之间太快，某些 USB 转串口模块
上电需要时间。在 `RetargetInit()` 后加 `HAL_Delay(100)` 缓解。

---

## 8. 文件清单

| 文件 | 性质 | 说明 |
|---|---|---|
| `Library/Inc/retarget.h` | **用户代码** | 接口声明：`RetargetInit` + `_write`/`_read` 等 6 个 newlib 桩 |
| `Library/Src/retarget.c` | **用户代码** | 全部实现：强符号 `_write`/`_read`/`_isatty`/`_close`/`_lseek`/`_fstat`，目标串口由 `gHuart` 决定 |
| `Core/Src/main.c` | CubeMX 生成 + 2 行用户代码 | `#include "retarget.h"`（Includes 区）、`RetargetInit(&huart1)`（USER CODE 2 区） |
| `Core/Src/usart.c` | CubeMX 生成 | USART1 初始化：115200-8N1、FIFO 关、PA9/PA10 AF7、时钟 D2PCLK2 |
| `Core/Src/syscalls.c` | CubeMX 生成 | 弱符号 `_write`/`_read` 桩，被 retarget.c 覆盖，**未改动** |
| `Library/CMakeLists.txt` | 构建 | `file(GLOB)` 自动收集，retarget.c / usb_stdio.c 共用一个 `user_library` |

**核心原则**：重定向实现在 `Library/`，CubeMX 生成的文件里只留
`USER CODE` 保护区内的 2 行 —— 可以随时用 CubeMX 重新生成而不丢代码。
