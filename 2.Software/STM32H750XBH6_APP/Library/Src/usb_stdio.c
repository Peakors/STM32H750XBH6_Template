/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_stdio.c
  * @brief          : printf()/scanf() over the USB CDC virtual COM port.
  ******************************************************************************
  * @attention
  *
  * This file is NOT CubeMX generated.  It contains all the USB virtual COM
  * port helpers, the only thing the generated USB_DEVICE/App/usbd_cdc_if.c
  * has to do is forward the received bytes:
  *
  *     void CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
  *     {
  *       usb_stdio_rx_push(Buf, *Len);          // <- the hook
  *       USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  *       USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  *     }
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usb_stdio.h"

#include <string.h>

#include "usb_device.h"
#include "usbd_cdc_if.h"

/* The USB device handle is defined in USB_DEVICE/App/usb_device.c and is not
   declared in any header, so declare it here.  If you rename it in CubeMX,
   this is the only line of this file that has to follow. */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* Private define ------------------------------------------------------------*/

#if ((USB_STDIO_RX_RING_SIZE & (USB_STDIO_RX_RING_SIZE - 1U)) != 0U)
#error "USB_STDIO_RX_RING_SIZE must be a power of two"
#endif
#if (USB_STDIO_TX_CHUNK_SIZE == 0U) || (USB_STDIO_TX_CHUNK_SIZE > 255U)
#error "USB_STDIO_TX_CHUNK_SIZE must be between 1 and 255"
#endif
#if (USB_STDIO_SCAN_LINE_SIZE < 2U)
#error "USB_STDIO_SCAN_LINE_SIZE must be at least 2"
#endif

/* Mask used for the power-of-two ring buffer wrap around */
#define USB_STDIO_RX_RING_MASK      (USB_STDIO_RX_RING_SIZE - 1U)

/* Private variables ---------------------------------------------------------*/

/* RX ring buffer.  head is only modified from the USB interrupt, tail only
   from the thread context. */
static uint8_t  usb_rx_ring[USB_STDIO_RX_RING_SIZE];
static volatile uint32_t usb_rx_head = 0U;      /* written by the ISR  */
static volatile uint32_t usb_rx_tail = 0U;      /* written by the user */
static volatile uint32_t usb_rx_overrun_cnt = 0U;
static volatile uint32_t usb_rx_last_tick = 0U; /* HAL tick of the last received byte */

/* Dedicated TX buffer used by the blocking helpers, so that they never clash
   with an application buffer that was handed to CDC_Transmit_FS() directly
   (that call is asynchronous). */
static uint8_t  usb_tx_buffer[USB_STDIO_TX_CHUNK_SIZE];

/* Private function prototypes -----------------------------------------------*/
static USBD_CDC_HandleTypeDef *usb_cdc_handle(void);
static uint32_t usb_tx_ready(uint32_t timeout_ms);
static uint32_t usb_tx(const uint8_t *buf, uint32_t len, uint32_t timeout_ms);
static int      usb_rx_pop(void);
static int      usb_rx_wait_byte(uint32_t first_timeout_ms, uint32_t inter_timeout_ms);

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Push bytes received on the USB OUT endpoint into the RX ring
  *         buffer.  Called from CDC_Receive_FS() (interrupt context).
  */
void usb_stdio_rx_push(const uint8_t *buf, uint32_t len)
{
  uint32_t i;

  if ((buf == NULL) || (len == 0U))
  {
    return;
  }

  for (i = 0U; i < len; i++)
  {
    uint32_t next = (usb_rx_head + 1U) & USB_STDIO_RX_RING_MASK;

    if (next == usb_rx_tail)
    {
      usb_rx_overrun_cnt++;   /* ring buffer full, drop the byte */
      continue;
    }

    usb_rx_ring[usb_rx_head] = buf[i];
    usb_rx_head = next;
  }

  usb_rx_last_tick = HAL_GetTick();
}

/**
  * @brief  Number of bytes currently waiting in the RX ring buffer.
  */
uint32_t usb_stdio_rx_available(void)
{
  return (usb_rx_head - usb_rx_tail) & USB_STDIO_RX_RING_MASK;
}

/**
  * @brief  Total number of bytes dropped because the ring buffer was full.
  */
uint32_t usb_stdio_rx_overrun(void)
{
  return usb_rx_overrun_cnt;
}

/**
  * @brief  Discard every byte currently waiting in the RX ring buffer.
  */
void usb_stdio_rx_flush(void)
{
  usb_rx_tail = usb_rx_head;
}

/* ------------------------------------------------------------------------- */
/* Raw USB CDC access                                                        */
/* ------------------------------------------------------------------------- */

/**
  * @brief  Return the CDC class handle, or NULL while the device is not
  *         initialised yet (i.e. usb_printf() called before
  *         MX_USB_DEVICE_Init()).
  */
static USBD_CDC_HandleTypeDef *usb_cdc_handle(void)
{
  return (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
}

/**
  * @brief  Wait until the IN endpoint is free (previous packet sent).
  * @param  timeout_ms: maximum time to wait, 0 to test only once
  * @retval 1 if the endpoint is ready, 0 on timeout / USB not ready
  */
static uint32_t usb_tx_ready(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  USBD_CDC_HandleTypeDef *hcdc;

  for (;;)
  {
    hcdc = usb_cdc_handle();
    if ((hcdc == NULL) || (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED))
    {
      return 0U;
    }

    if (hcdc->TxState == 0U)
    {
      return 1U;
    }

    if ((timeout_ms == 0U) || ((HAL_GetTick() - start) >= timeout_ms))
    {
      return 0U;
    }

    /* Never spin here without feeding the USB interrupt. */
    HAL_Delay(1U);
  }
}

/**
  * @brief  Blocking write to the USB virtual COM port.
  *         The buffer is split into USB_STDIO_TX_CHUNK_SIZE blocks and the
  *         function only returns once the last block has been handed over to
  *         the USB IN endpoint, so the caller may reuse/free the buffer.
  * @param  buf: data to send
  * @param  len: number of bytes to send
  * @param  timeout_ms: maximum wait for a free IN endpoint per block
  * @retval number of bytes actually sent
  */
static uint32_t usb_tx(const uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
  uint32_t sent = 0U;

  if ((buf == NULL) || (len == 0U))
  {
    return 0U;
  }

  while (sent < len)
  {
    uint32_t chunk = len - sent;

    if (chunk > USB_STDIO_TX_CHUNK_SIZE)
    {
      chunk = USB_STDIO_TX_CHUNK_SIZE;
    }

    if (usb_tx_ready(timeout_ms) == 0U)
    {
      break;
    }

    /* usb_tx_buffer holds the pending data, it must not be overwritten while
       TxState is set - guaranteed by the wait above. */
    (void)memcpy(usb_tx_buffer, &buf[sent], chunk);

    /* CDC_Transmit_FS() may still answer USBD_BUSY here: TxState can be
       raised again between the check above and the call (the completion
       interrupt of the previous packet may not have been serviced yet).
       Retrying is mandatory - treating USBD_BUSY as a hard error silently
       truncated every transfer longer than USB_STDIO_TX_CHUNK_SIZE bytes. */
    {
      uint32_t delay = 1U;
      uint8_t status;
      uint32_t waited = 0U;

      for (;;)
      {
        status = CDC_Transmit_FS(usb_tx_buffer, (uint16_t)chunk);
        if (status == USBD_OK)
        {
          break;
        }
        if ((status != USBD_BUSY) || (waited >= timeout_ms))
        {
          return sent;                  /* real error or timeout: stop */
        }

        HAL_Delay(delay);               /* let the USB interrupt progress */
        waited += delay;
        if (delay < 8U)
        {
          delay <<= 1U;                 /* 1,2,4,8,8,... ms backoff */
        }
      }

      /* Give the completion interrupt a chance to run before the next
         chunk, so the busy/ready race is not hit once per chunk. */
      HAL_Delay(1U);
    }

    sent += chunk;
  }

  return sent;
}

/**
  * @brief  Blocking write of a raw buffer over USB.
  */
uint32_t usb_write(const uint8_t *buf, uint32_t len)
{
  return usb_tx(buf, len, USB_STDIO_TX_TIMEOUT_MS);
}

/**
  * @brief  Pop one byte out of the RX ring buffer.
  * @retval byte value 0..255, or -1 when the ring buffer is empty
  */
static int usb_rx_pop(void)
{
  int value = -1;

  if (usb_rx_tail != usb_rx_head)
  {
    value = (int)usb_rx_ring[usb_rx_tail];
    usb_rx_tail = (usb_rx_tail + 1U) & USB_STDIO_RX_RING_MASK;
  }

  return value;
}

/**
  * @brief  Wait for the next received byte.
  * @param  first_timeout_ms: how long to wait for the first byte
  * @param  inter_timeout_ms: how long to wait for follow-up bytes
  * @retval byte value 0..255, or -1 on timeout
  */
static int usb_rx_wait_byte(uint32_t first_timeout_ms, uint32_t inter_timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint32_t timeout = first_timeout_ms;
  uint32_t inter = 0U;   /* 1 once the shorter inter-byte timeout is active */

  for (;;)
  {
    int value = usb_rx_pop();

    if (value >= 0)
    {
      return value;
    }

    if (inter == 0U)
    {
      /* Anchor the timeout on the first byte that reaches the ring buffer. */
      if (usb_rx_last_tick != 0U)
      {
        start = usb_rx_last_tick;
        timeout = inter_timeout_ms;
        inter = 1U;
      }
    }
    else if (usb_rx_last_tick != start)
    {
      start = usb_rx_last_tick;
      timeout = inter_timeout_ms;
    }

    if ((timeout == 0U) || ((HAL_GetTick() - start) >= timeout))
    {
      return -1;
    }

    HAL_Delay(1U);
  }
}

/**
  * @brief  Blocking read of raw bytes from the USB virtual COM port.
  */
uint32_t usb_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
  uint32_t count = 0U;
  int value;

  if ((buf == NULL) || (len == 0U))
  {
    return 0U;
  }

  value = usb_rx_wait_byte(timeout_ms, timeout_ms);
  if (value < 0)
  {
    return 0U;
  }

  buf[count++] = (uint8_t)value;

  /* Drain whatever is already buffered without adding any extra delay. */
  while (count < len)
  {
    value = usb_rx_pop();
    if (value < 0)
    {
      break;
    }
    buf[count++] = (uint8_t)value;
  }

  return count;
}

/**
  * @brief  Blocking single character input.
  */
int usb_getchar(void)
{
  return usb_rx_wait_byte(USB_STDIO_RX_TIMEOUT_MS, USB_STDIO_RX_TIMEOUT_MS);
}

/**
  * @brief  Send a single character.
  */
int usb_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;

  if (usb_tx(&byte, 1U, USB_STDIO_TX_TIMEOUT_MS) != 1U)
  {
    return -1;
  }

  return (int)byte;
}

/* ------------------------------------------------------------------------- */
/* printf() over USB                                                         */
/* ------------------------------------------------------------------------- */

/**
  * @brief  printf() over the USB virtual COM port.
  */
int usb_vprintf(const char *format, va_list args)
{
  char buffer[USB_STDIO_TX_CHUNK_SIZE];
  int written;

  if (format == NULL)
  {
    return -1;
  }

  written = vsnprintf(buffer, sizeof(buffer), format, args);
  if (written <= 0)
  {
    return written;
  }

  /* vsnprintf() reports the length it would have needed, clamp it to what
     actually fits into the buffer. */
  if ((uint32_t)written > (uint32_t)(sizeof(buffer) - 1U))
  {
    written = (int)(sizeof(buffer) - 1U);
  }

  (void)usb_tx((const uint8_t *)buffer, (uint32_t)written, USB_STDIO_TX_TIMEOUT_MS);

  return written;
}

/**
  * @brief  printf() over the USB virtual COM port.
  */
int usb_printf(const char *format, ...)
{
  va_list args;
  int result;

  va_start(args, format);
  result = usb_vprintf(format, args);
  va_end(args);

  return result;
}

/* ------------------------------------------------------------------------- */
/* scanf() over USB                                                          */
/* ------------------------------------------------------------------------- */

/**
  * @brief  scanf() over the USB virtual COM port.
  */
int usb_vscanf(const char *format, va_list args)
{
  char line[USB_STDIO_SCAN_LINE_SIZE];
  uint32_t length = 0U;
  int value;

  if (format == NULL)
  {
    return -1;
  }

  /* Wait for the first character (blocking, RX timeout applies). */
  value = usb_getchar();
  if (value < 0)
  {
    return 0;
  }

  /* On most terminals the host sends the whole line at once, so a short
     inter-byte timeout is enough to regroup it. */
  do
  {
    if (length < (USB_STDIO_SCAN_LINE_SIZE - 1U))
    {
      line[length++] = (char)value;
    }
    value = usb_rx_wait_byte(0U, USB_STDIO_LINE_GAP_MS);
  } while (value >= 0);

  line[length] = '\0';

  if (length == 0U)
  {
    return 0;
  }

  return vsscanf(line, format, args);
}

/**
  * @brief  scanf() over the USB virtual COM port.
  */
int usb_scanf(const char *format, ...)
{
  va_list args;
  int result;

  va_start(args, format);
  result = usb_vscanf(format, args);
  va_end(args);

  return result;
}

/* ------------------------------------------------------------------------- */
/* Optional stdio retarget                                                   */
/* ------------------------------------------------------------------------- */
/* Core/Src/syscalls.c declares __io_putchar()/__io_getchar() weak and routes
   its weak _write()/_read() through them, so defining them here is enough to
   make the standard printf()/scanf() usable over USB.

   NOTE: Library/Src/retarget.c defines a STRONG _write()/_read() pair that
   sends printf() to USART1 instead.  Define USB_STDIO_RETARGET to install the
   weak character hooks, or USB_STDIO_RETARGET_FORCE to also define
   _write()/_read() here and deliberately override retarget.c (the linker then
   reports a multiple definition warning and RetargetInit() in main.c becomes
   useless).                                                            */
#if defined(USB_STDIO_RETARGET) || defined(USB_STDIO_RETARGET_FORCE)

/** @brief Character output hook used by _write() in Core/Src/syscalls.c. */
int __io_putchar(int ch)
{
  return usb_putchar(ch);
}

/** @brief Character input hook used by _read() in Core/Src/syscalls.c. */
int __io_getchar(void)
{
  int value = usb_getchar();

  return (value < 0) ? 0 : value;
}

#ifdef USB_STDIO_RETARGET_FORCE
/* _write()/stdout is block oriented, send the whole block at once. */
int _write(int file, char *ptr, int len)
{
  (void)file;

  if ((ptr == NULL) || (len <= 0))
  {
    return 0;
  }

  return (int)usb_tx((const uint8_t *)ptr, (uint32_t)len, USB_STDIO_TX_TIMEOUT_MS);
}

int _read(int file, char *ptr, int len)
{
  (void)file;

  if ((ptr == NULL) || (len <= 0))
  {
    return 0;
  }

  return (int)usb_read((uint8_t *)ptr, (uint32_t)len, USB_STDIO_RX_TIMEOUT_MS);
}
#endif /* USB_STDIO_RETARGET_FORCE */

#endif /* USB_STDIO_RETARGET || USB_STDIO_RETARGET_FORCE */
