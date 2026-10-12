//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#ifndef SDIO_LLD_H
#define SDIO_LLD_H

#if (HAL_NF_USE_STM32_SDIO == TRUE)

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

// DMA stream used for SDIO (DMA2 stream 3 or 6)
#if !defined(STM32_SDIO_NF_DMA_STREAM)
#define STM32_SDIO_NF_DMA_STREAM STM32_DMA_STREAM_ID(2, 3)
#endif

// SDIO DMA priority (0..3|lowest..highest)
#if !defined(STM32_SDIO_NF_DMA_PRIORITY)
#define STM32_SDIO_NF_DMA_PRIORITY 3
#endif

// SDIO interrupt priority level setting
#if !defined(STM32_SDIO_NF_IRQ_PRIORITY)
#define STM32_SDIO_NF_IRQ_PRIORITY 9
#endif

// Upper limit for the SDIO bus clock (Hz)
#if !defined(STM32_SDIO_NF_MAX_CLOCK)
#define STM32_SDIO_NF_MAX_CLOCK 25000000U
#endif

// Default data transfer timeout (ms)
#if !defined(STM32_SDIO_NF_DATA_TIMEOUT_MS)
#define STM32_SDIO_NF_DATA_TIMEOUT_MS 1000U
#endif

// Delay after powering on the SDIO peripheral before sending commands (ms)
#if !defined(STM32_SDIO_NF_CLOCK_ACTIVATION_DELAY)
#define STM32_SDIO_NF_CLOCK_ACTIVATION_DELAY 10U
#endif

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

#if !defined(STM32_HAS_SDIO) || (STM32_HAS_SDIO == FALSE)
#error "SDIO not present in the selected device"
#endif

#if !STM32_DMA_IS_VALID_ID(STM32_SDIO_NF_DMA_STREAM, STM32_SDC_SDIO_DMA_MSK)
#error "invalid DMA stream associated to SDIO"
#endif

#if !OSAL_IRQ_IS_VALID_PRIORITY(STM32_SDIO_NF_IRQ_PRIORITY)
#error "Invalid IRQ priority assigned to SDIO"
#endif

#if !STM32_DMA_IS_VALID_PRIORITY(STM32_SDIO_NF_DMA_PRIORITY)
#error "Invalid DMA priority assigned to SDIO"
#endif

// SDIO kernel clock
#if defined(STM32_SDIOSEL_SYSCLK) && defined(STM32_SDIOSEL)
#if (STM32_SDIOSEL == STM32_SDIOSEL_SYSCLK)
#define STM32_SDIO_NF_SDIOCLK STM32_SYSCLK
#endif
#endif

#if !defined(STM32_SDIO_NF_SDIOCLK)
#define STM32_SDIO_NF_SDIOCLK STM32_PLL48CLK
#endif

#if (STM32_SDIO_NF_SDIOCLK == 0)
#error "SDIO kernel clock is not running, check PLL48CLK configuration"
#endif

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

struct SDIODriver
{
    // driver state
    sdiostate_t state;

    // current configuration data
    const SDIOConfig *config;

    // accumulated errors (SDIO_xxx_ERROR flags)
    uint32_t errors;

    // card interrupt callback
    sdiocallback_t cardInterruptCallback;

#if (SDIO_USE_MUTUAL_EXCLUSION == TRUE)
    // mutex protecting the bus
    mutex_t mutex;
#endif

    // thread waiting for the end of a data transfer
    thread_reference_t thread;

    // pointer to the SDIO registers block
    SDIO_TypeDef *sdio;

    // DMA stream
    const stm32_dma_stream_t *dma;

    // DMA mode bit mask
    uint32_t dmaMode;

    // shadow copy of the CLKCR register
    uint32_t clkcr;
};

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

extern SDIODriver SDIOD1;

#ifdef __cplusplus
extern "C"
{
#endif

    void sdio_lld_init(void);
    void sdio_lld_start(SDIODriver *sdiop);
    void sdio_lld_start_clock(SDIODriver *sdiop);
    void sdio_lld_stop(SDIODriver *sdiop);
    void sdio_lld_set_clock(SDIODriver *sdiop, uint32_t frequency);
    void sdio_lld_set_bus_width(SDIODriver *sdiop, sdiobuswidth_t width);
    bool sdio_lld_send_cmd(
        SDIODriver *sdiop,
        uint8_t command,
        uint32_t argument,
        sdioresponse_t responseType,
        uint32_t *response);
    bool sdio_lld_transfer(
        SDIODriver *sdiop,
        sdiodirection_t direction,
        uint32_t argument,
        uint8_t *buffer,
        uint32_t length,
        uint32_t blockSize,
        uint32_t *response);
    void sdio_lld_enable_card_interrupt(SDIODriver *sdiop, bool enable);

#ifdef __cplusplus
}
#endif

#endif // HAL_NF_USE_STM32_SDIO

#endif // SDIO_LLD_H
