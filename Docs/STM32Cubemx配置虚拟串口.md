# STM32Cubemx配置虚拟串口

## 虚拟串口简介

USB 虚拟串口，简称 VCP，是 Virtual COM Port 的简写，它是利用 USB 的 CDC 类来实现的一种通信接口。

我们可以利用 STM32 自带的 USB 功能，来实现一个 USB 虚拟串口，从而通过 USB，实现电脑与 STM32 的数据互传。上位机无需编写专门的 USB 程序，只需要一个串口调试助手即可调试，非常实用。

我们以`STM32H750XBH6`为例介绍详细配置过程。

## STM32CubeMX配置

### Connectivity -> USB_OTG_FS -> Mode: Device_Only

![image-20250806192004306](Pictures\image-20250806192004306.png)

### Middleware -> USB_DEVICE -> Class For FS IP: Communication Device Class (Virtual Port Com)

![image-20250806192243922](Pictures\image-20250806192243922.png)

### Clock Configuration -> USB Clock Mux -> 48MHz

![image-20250806192414615](D:\clion_stm32\STM32H750XBH6_Template\Docs\Pictures\image-20250806192414615.png)

## 编写程序

CubeMX会自动生成如下文件

![image-20250806192603174](Pictures\image-20250806192603174.png)

`main.c` 中包含头文件

```c
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h" 			//USB虚拟串口
/* USER CODE END Includes */
```

编写测试代码

```c
...
/* USER CODE BEGIN PV */
unsigned char buff[20] = {"USB_CDC_Test\r\n"};
/* USER CODE END PV */
...
...
...
while (1)
{
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
    CDC_Transmit_FS(buff,sizeof(buff));
    HAL_Delay(500);
}   
/* USER CODE END 3 */
...
```

## 虚拟串口重定向usb_printf

在`usbd_cdc_if.c`中添加

```c
...
/* USER CODE BEGIN INCLUDE */
#include "stdarg.h"
/* USER CODE END INCLUDE */
...
...
...
/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
void usb_printf(const char *format, ...);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */
...
...
...
/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
void usb_printf(const char *format, ...)
{
  va_list args;
  uint32_t length;

  va_start(args, format);
  length = vsnprintf((char *)UserTxBufferFS, APP_TX_DATA_SIZE, (char *)format, args);
  va_end(args);
  CDC_Transmit_FS(UserTxBufferFS, length);
}
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */
...
```

编写测试代码

```c
...
/* USER CODE BEGIN PV */
unsigned char buff[20] = {"USB_CDC_Test\r\n"};
float f_var = 1.0F;
/* USER CODE END PV */
...
...
...
while (1)
{
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
    usb_printf("%s\r\n", buff);
    usb_printf("%.5f\r\n", f_var);
    HAL_Delay(500);
}   
/* USER CODE END 3 */
...
```

