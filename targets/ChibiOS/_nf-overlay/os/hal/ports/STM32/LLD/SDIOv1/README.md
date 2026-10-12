# SDIO v1

SD I/O card host driver for the STM32 `SDIO` peripheral (STM32F2 and STM32F4 series).

The ChibiOS SDC driver only handles memory cards. This driver implements the SDIO card protocol on top of the same peripheral: `CMD52` (IO_RW_DIRECT), `CMD53` (IO_RW_EXTENDED) in byte and block mode and the in-band card interrupt. It's meant to host SDIO Wi-Fi chips (e.g. BCM43362 on the MXCHIP EMW3166 module).

The ChibiOS driver follows the 'standard' template driver model. The high level API is in `hal_stm32_sdio.h`.

## Configuration

Enable the driver in `halconf_nf.h`:

```c
#define HAL_NF_USE_STM32_SDIO TRUE
```

Optional settings for `mcuconf_nf.h` (defaults shown):

```c
#define STM32_SDIO_NF_DMA_STREAM             STM32_DMA_STREAM_ID(2, 3) // DMA2 stream 3 or 6
#define STM32_SDIO_NF_DMA_PRIORITY           3
#define STM32_SDIO_NF_IRQ_PRIORITY           9
#define STM32_SDIO_NF_MAX_CLOCK              25000000U
#define STM32_SDIO_NF_DATA_TIMEOUT_MS        1000U
#define STM32_SDIO_NF_CLOCK_ACTIVATION_DELAY 10U
```

The SDIO kernel clock is `PLL48CLK` (or `SYSCLK` when `STM32_SDIOSEL` selects it) and has to be running.

## Usage notes

- This driver and the ChibiOS SDC driver (`HAL_USE_SDC`) can't be enabled at the same time, both own the SDIO peripheral and its IRQ handler.
- Card enumeration (`CMD0`, `CMD5`, `CMD3`, `CMD7`) is up to the client, using `sdioSendCmd()`. `CMD5` returns an R4 response, use `SDIO_RESPONSE_SHORT_NO_CRC` for it.
- `sdioStart()` leaves the bus at 400kHz with 1 bit width. After enumeration switch the card (CCCR) and then call `sdioSetBusWidth()` and `sdioSetClock()`.
- Data buffers have to be 32 bits aligned. For reads the buffer size has to be rounded up to a multiple of 4 bytes because the DMA moves whole words from the SDIO FIFO.
- Byte mode transfers (`blockSize == 0`) with a power of 2 length run the data path in block mode with a single block of that length (as the WICED STM32 driver does). Other lengths (1 to 512 bytes) use the SDIO multibyte mode. Block mode requires a power of 2 block size.
- The card interrupt callback is called from ISR context and the card interrupt is disabled before calling it. Call `sdioEnableCardInterrupt()` again to re-arm it.
- Hardware flow control and the clock dephasing (`NEGEDGE`) mode are not used because of device errata.
- The R4 response to `CMD5` has no valid CRC and the peripheral flags it with `CCRCFAIL`. This is expected and handled by `SDIO_RESPONSE_SHORT_NO_CRC`.
