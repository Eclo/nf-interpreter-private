//
// Copyright (c) .NET Foundation and Contributors
// Portions Copyright (c) 2006-2026 Giovanni Di Sirio. All rights reserved.
// See LICENSE file in the project root for full license information.
//

// SD I/O card host driver for the STM32 SDIO peripheral (F2/F4 series).
// Implements the SDIO card protocol: CMD52, CMD53 in byte and block mode and the card interrupt.

#include <hal.h>
#include <hal_nf_community.h>

#if (HAL_NF_USE_STM32_SDIO == TRUE)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

// some of these are missing from some device headers, fixing as zeroed masks
#if !defined(SDIO_STA_STBITERR)
#define SDIO_STA_STBITERR 0
#endif

#if !defined(SDIO_MASK_STBITERRIE)
#define SDIO_MASK_STBITERRIE 0
#endif

#define SDIO_NF_ICR_ALL_FLAGS 0xFFFFFFFFU

// command path status flags
#define SDIO_NF_STA_CMD_FLAGS (SDIO_STA_CCRCFAIL | SDIO_STA_CTIMEOUT | SDIO_STA_CMDREND | SDIO_STA_CMDSENT)

// data path status flags (MASK bits have the same positions as the STA bits)
#define SDIO_NF_STA_DATA_FLAGS                                                                                         \
    (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR | SDIO_STA_DATAEND |                 \
     SDIO_STA_DBCKEND | SDIO_STA_STBITERR)

// data path interrupts used to wake up the waiting thread
#define SDIO_NF_MASK_DATA                                                                                              \
    (SDIO_MASK_DCRCFAILIE | SDIO_MASK_DTIMEOUTIE | SDIO_MASK_TXUNDERRIE | SDIO_MASK_RXOVERRIE | SDIO_MASK_DATAENDIE |  \
     SDIO_MASK_STBITERRIE)

#define SDIO_NF_DMA_CHANNEL STM32_DMA_GETCHANNEL(STM32_SDIO_NF_DMA_STREAM, STM32_SDC_SDIO_DMA_CHN)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

SDIODriver SDIOD1;

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

// Calculates the CLKCR clock divider bits for the requested bus frequency.
// SDIO_CK = SDIOCLK / (CLKDIV + 2), or SDIOCLK with bypass.
// The divider is rounded so the requested frequency is never exceeded.
static uint32_t sdio_lld_clock_bits(uint32_t frequency)
{
    uint32_t divider;

    if (frequency > STM32_SDIO_NF_MAX_CLOCK)
    {
        frequency = STM32_SDIO_NF_MAX_CLOCK;
    }

    if (frequency >= STM32_SDIO_NF_SDIOCLK)
    {
        return SDIO_CLKCR_BYPASS;
    }

    divider = (STM32_SDIO_NF_SDIOCLK + frequency - 1U) / frequency;

    if (divider < 2U)
    {
        divider = 2U;
    }

    if (divider > (SDIO_CLKCR_CLKDIV_Msk + 2U))
    {
        divider = SDIO_CLKCR_CLKDIV_Msk + 2U;
    }

    return divider - 2U;
}

// Writes the CLKCR register.
// CLKCR can't be written again within 3 SDIOCLK + 2 PCLK2 clock periods after a write, so a shadow copy is kept
// (read-modify-write sequences never use a stale value) and the write is followed by a short delay.
static void sdio_lld_write_clkcr(SDIODriver *sdiop, uint32_t value)
{
    sdiop->clkcr = value;
    sdiop->sdio->CLKCR = value;

    // well over 3 SDIOCLK + 2 PCLK2 clock periods
    for (volatile uint32_t i = 0; i < 16U; i++)
    {
    }
}

// Calculates the DTIMER value (in bus clock cycles) for a timeout in milliseconds.
static uint32_t sdio_lld_get_data_timeout(SDIODriver *sdiop, uint32_t timeoutMs)
{
    uint32_t clkcr = sdiop->clkcr;
    uint32_t busClock;

    if ((clkcr & SDIO_CLKCR_BYPASS) != 0U)
    {
        busClock = STM32_SDIO_NF_SDIOCLK;
    }
    else
    {
        busClock = STM32_SDIO_NF_SDIOCLK / ((clkcr & SDIO_CLKCR_CLKDIV_Msk) + 2U);
    }

    return (busClock / 1000U) * timeoutMs;
}

static uint32_t sdio_lld_get_timeout_ms(SDIODriver *sdiop)
{
    if (sdiop->config != NULL && sdiop->config->dataTimeoutMs != 0U)
    {
        return sdiop->config->dataTimeoutMs;
    }

    return STM32_SDIO_NF_DATA_TIMEOUT_MS;
}

static void sdio_lld_collect_errors(SDIODriver *sdiop, uint32_t sta)
{
    uint32_t errors = SDIO_NO_ERROR;

    if (sta & SDIO_STA_CCRCFAIL)
        errors |= SDIO_CMD_CRC_ERROR;
    if (sta & SDIO_STA_CTIMEOUT)
        errors |= SDIO_CMD_TIMEOUT;
    if (sta & SDIO_STA_DCRCFAIL)
        errors |= SDIO_DATA_CRC_ERROR;
    if (sta & SDIO_STA_DTIMEOUT)
        errors |= SDIO_DATA_TIMEOUT;
    if (sta & SDIO_STA_TXUNDERR)
        errors |= SDIO_TX_UNDERRUN;
    if (sta & SDIO_STA_RXOVERR)
        errors |= SDIO_RX_OVERRUN;
    if (sta & SDIO_STA_STBITERR)
        errors |= SDIO_STARTBIT_ERROR;

    sdiop->errors |= errors;
}

// DCTRL value with the data path idle: SDIOEN is required for the card interrupt (DAT1) to be detected.
static inline uint32_t sdio_lld_idle_dctrl(SDIODriver *sdiop)
{
    return ((sdiop->sdio->MASK & SDIO_MASK_SDIOITIE) != 0U) ? SDIO_DCTRL_SDIOEN : 0U;
}

// Stops the data path and DMA after a failed transfer.
static void sdio_lld_data_error_cleanup(SDIODriver *sdiop)
{
    uint32_t sta;

    dmaStreamDisable(sdiop->dma);

    osalSysLock();
    sdiop->sdio->MASK &= SDIO_MASK_SDIOITIE;
    osalSysUnlock();

    sta = sdiop->sdio->STA;
    sdiop->sdio->DCTRL = sdio_lld_idle_dctrl(sdiop);
    sdiop->sdio->ICR = sta & SDIO_NF_STA_DATA_FLAGS;

    sdio_lld_collect_errors(sdiop, sta);
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

#if !defined(STM32_SDIO_HANDLER)
#error "STM32_SDIO_HANDLER not defined"
#endif

// SDIO IRQ handler.
// Wakes up the thread waiting for a data transfer and dispatches the card interrupt.
// Error handling for data transfers is performed in the thread.
OSAL_IRQ_HANDLER(STM32_SDIO_HANDLER)
{
    uint32_t sta;
    uint32_t mask;
    bool cardInterrupt;

    OSAL_IRQ_PROLOGUE();

    osalSysLockFromISR();

    sta = SDIO->STA;
    mask = SDIO->MASK;

    // card interrupt is one-shot: disable it until the client re-arms it
    cardInterrupt = ((mask & SDIO_MASK_SDIOITIE) != 0U) && ((sta & SDIO_STA_SDIOIT) != 0U);

    if (cardInterrupt)
    {
        mask &= ~SDIO_MASK_SDIOITIE;
        SDIO->ICR = SDIO_ICR_SDIOITC;
    }

    // data path: disable the sources but leave the status flags for the thread to check
    if ((sta & mask & SDIO_NF_MASK_DATA) != 0U)
    {
        mask &= ~SDIO_NF_MASK_DATA;
        osalThreadResumeI(&SDIOD1.thread, MSG_OK);
    }

    SDIO->MASK = mask;

    osalSysUnlockFromISR();

    if (cardInterrupt && SDIOD1.cardInterruptCallback != NULL)
    {
        SDIOD1.cardInterruptCallback(&SDIOD1);
    }

    OSAL_IRQ_EPILOGUE();
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

void sdio_lld_init(void)
{
    sdioObjectInit(&SDIOD1);
    SDIOD1.thread = NULL;
    SDIOD1.dma = NULL;
    SDIOD1.sdio = SDIO;

    nvicEnableVector(STM32_SDIO_NUMBER, STM32_SDIO_NF_IRQ_PRIORITY);
}

// Configures and activates the SDIO peripheral, bus clock stopped.
// Has to be called with the system locked.
void sdio_lld_start(SDIODriver *sdiop)
{
    sdiop->dmaMode = STM32_DMA_CR_CHSEL(SDIO_NF_DMA_CHANNEL) | STM32_DMA_CR_PL(STM32_SDIO_NF_DMA_PRIORITY) |
                     STM32_DMA_CR_PSIZE_WORD | STM32_DMA_CR_MSIZE_WORD | STM32_DMA_CR_MINC | STM32_DMA_CR_PFCTRL |
                     STM32_DMA_CR_PBURST_INCR4 | STM32_DMA_CR_MBURST_INCR4;

    if (sdiop->state == SDIO_STOP)
    {
        sdiop->dma = dmaStreamAllocI(STM32_SDIO_NF_DMA_STREAM, STM32_SDIO_NF_IRQ_PRIORITY, NULL, NULL);
        osalDbgAssert(sdiop->dma != NULL, "unable to allocate stream");

        dmaStreamSetPeripheral(sdiop->dma, &sdiop->sdio->FIFO);
        dmaStreamSetFIFO(sdiop->dma, STM32_DMA_FCR_DMDIS | STM32_DMA_FCR_FTH_FULL);

        rccEnableSDIO(true);
    }

    sdiop->sdio->POWER = 0U;
    sdio_lld_write_clkcr(sdiop, 0U);
    sdiop->sdio->DCTRL = 0U;
    sdiop->sdio->DTIMER = 0U;
    sdiop->sdio->MASK = 0U;
    sdiop->sdio->ICR = SDIO_NF_ICR_ALL_FLAGS;
}

// Powers on the card bus and starts the clock in identification mode (400kHz, 1 bit).
// Must NOT be called with the system locked.
void sdio_lld_start_clock(SDIODriver *sdiop)
{
    sdio_lld_write_clkcr(sdiop, sdio_lld_clock_bits(400000U));
    sdiop->sdio->POWER |= SDIO_POWER_PWRCTRL_0 | SDIO_POWER_PWRCTRL_1;
    sdio_lld_write_clkcr(sdiop, sdiop->clkcr | SDIO_CLKCR_CLKEN);

    // clock activation delay (also covers the 74 clock cycles required before the first command)
    osalThreadSleep(OSAL_MS2I(STM32_SDIO_NF_CLOCK_ACTIVATION_DELAY));
}

// Deactivates the SDIO peripheral.
// Has to be called with the system locked.
void sdio_lld_stop(SDIODriver *sdiop)
{
    if (sdiop->state != SDIO_STOP)
    {
        sdiop->sdio->POWER = 0U;
        sdio_lld_write_clkcr(sdiop, 0U);
        sdiop->sdio->DCTRL = 0U;
        sdiop->sdio->DTIMER = 0U;
        sdiop->sdio->MASK = 0U;
        sdiop->sdio->ICR = SDIO_NF_ICR_ALL_FLAGS;

        dmaStreamFreeI(sdiop->dma);
        sdiop->dma = NULL;

        rccDisableSDIO();
    }
}

void sdio_lld_set_clock(SDIODriver *sdiop, uint32_t frequency)
{
    sdio_lld_write_clkcr(
        sdiop,
        (sdiop->clkcr & ~(SDIO_CLKCR_BYPASS | SDIO_CLKCR_CLKDIV_Msk)) | sdio_lld_clock_bits(frequency));
}

void sdio_lld_set_bus_width(SDIODriver *sdiop, sdiobuswidth_t width)
{
    uint32_t clkcr = sdiop->clkcr & ~SDIO_CLKCR_WIDBUS;

    if (width == SDIO_BUS_WIDTH_4BIT)
    {
        clkcr |= SDIO_CLKCR_WIDBUS_0;
    }

    sdio_lld_write_clkcr(sdiop, clkcr);
}

// Sends a command and waits for the response (polled, the command path has a hardware timeout).
bool sdio_lld_send_cmd(
    SDIODriver *sdiop,
    uint8_t command,
    uint32_t argument,
    sdioresponse_t responseType,
    uint32_t *response)
{
    uint32_t cmd = (uint32_t)command | SDIO_CMD_CPSMEN;
    uint32_t waitFlags;
    uint32_t sta;

    switch (responseType)
    {
        case SDIO_RESPONSE_NONE:
            waitFlags = SDIO_STA_CMDSENT;
            break;

        case SDIO_RESPONSE_LONG:
            cmd |= SDIO_CMD_WAITRESP_0 | SDIO_CMD_WAITRESP_1;
            waitFlags = SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL;
            break;

        default:
            cmd |= SDIO_CMD_WAITRESP_0;
            waitFlags = SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL;
            break;
    }

    // wait for any previous command to complete
    while ((sdiop->sdio->STA & SDIO_STA_CMDACT) != 0U)
    {
    }

    sdiop->sdio->ICR = SDIO_NF_STA_CMD_FLAGS;
    sdiop->sdio->ARG = argument;
    sdiop->sdio->CMD = cmd;

    while (((sta = sdiop->sdio->STA) & waitFlags) == 0U)
    {
    }

    sdiop->sdio->ICR = sta & SDIO_NF_STA_CMD_FLAGS;

    if (responseType == SDIO_RESPONSE_NONE)
    {
        return HAL_SUCCESS;
    }

    if ((sta & SDIO_STA_CTIMEOUT) != 0U)
    {
        sdio_lld_collect_errors(sdiop, sta & SDIO_STA_CTIMEOUT);
        return HAL_FAILED;
    }

    // R3/R4 responses carry no valid CRC, the peripheral flags them with CCRCFAIL (expected, see device errata)
    if (((sta & SDIO_STA_CCRCFAIL) != 0U) && (responseType != SDIO_RESPONSE_SHORT_NO_CRC))
    {
        sdio_lld_collect_errors(sdiop, sta & SDIO_STA_CCRCFAIL);
        return HAL_FAILED;
    }

    if (response != NULL)
    {
        if (responseType == SDIO_RESPONSE_LONG)
        {
            // save words in reverse order because MSB in response comes first
            response[0] = sdiop->sdio->RESP4;
            response[1] = sdiop->sdio->RESP3;
            response[2] = sdiop->sdio->RESP2;
            response[3] = sdiop->sdio->RESP1;
        }
        else
        {
            *response = sdiop->sdio->RESP1;
        }
    }

    return HAL_SUCCESS;
}

// Performs a CMD53 data transfer using DMA.
// blockSize == 0 selects byte (multibyte) mode, otherwise block mode with a power of 2 block size.
// The buffer has to be 32 bits aligned and, for reads, its size rounded up to a multiple of 4 bytes
// because the DMA moves whole words from the SDIO FIFO.
bool sdio_lld_transfer(
    SDIODriver *sdiop,
    sdiodirection_t direction,
    uint32_t argument,
    uint8_t *buffer,
    uint32_t length,
    uint32_t blockSize,
    uint32_t *response)
{
    uint32_t timeoutMs = sdio_lld_get_timeout_ms(sdiop);
    uint32_t dctrl = SDIO_DCTRL_SDIOEN | SDIO_DCTRL_DMAEN | SDIO_DCTRL_DTEN;
    msg_t msg = MSG_OK;

    osalDbgCheck((((uint32_t)buffer) & 3U) == 0U);

    if (blockSize == 0U)
    {
        if ((length & (length - 1U)) == 0U)
        {
            // byte mode with a power of 2 length: run the data path in block mode with a single block of that size
            // (same as the WICED STM32 driver, proven with these WLAN chips)
            dctrl |= ((uint32_t)__builtin_ctz(length) << SDIO_DCTRL_DBLOCKSIZE_Pos) & SDIO_DCTRL_DBLOCKSIZE_Msk;
        }
        else
        {
            // SDIO multibyte transfer
            dctrl |= SDIO_DCTRL_DTMODE;
        }
    }
    else
    {
        dctrl |= ((uint32_t)__builtin_ctz(blockSize) << SDIO_DCTRL_DBLOCKSIZE_Pos) & SDIO_DCTRL_DBLOCKSIZE_Msk;
    }

    if (direction == SDIO_DIRECTION_READ)
    {
        dctrl |= SDIO_DCTRL_DTDIR;
    }

    sdiop->sdio->DTIMER = sdio_lld_get_data_timeout(sdiop, timeoutMs);
    sdiop->sdio->DLEN = length;

    // DMA with peripheral flow control, the transaction size is ignored by the stream
    dmaStreamSetMemory0(sdiop->dma, buffer);
    dmaStreamSetTransactionSize(sdiop->dma, (length + 3U) / sizeof(uint32_t));
    dmaStreamSetMode(
        sdiop->dma,
        sdiop->dmaMode | (direction == SDIO_DIRECTION_READ ? STM32_DMA_CR_DIR_P2M : STM32_DMA_CR_DIR_M2P));
    dmaStreamEnable(sdiop->dma);

    // arm the data path interrupts (keep the card interrupt enable as it is)
    osalSysLock();
    sdiop->sdio->ICR = SDIO_NF_STA_DATA_FLAGS;
    sdiop->sdio->MASK = (sdiop->sdio->MASK & SDIO_MASK_SDIOITIE) | SDIO_NF_MASK_DATA;
    osalSysUnlock();

    // for reads the data path has to be waiting for the start bit before the command is sent
    // because the card can start sending data right after the response
    if (direction == SDIO_DIRECTION_READ)
    {
        sdiop->sdio->DCTRL = dctrl;
    }

    if (sdio_lld_send_cmd(sdiop, SDIO_CMD_IO_RW_EXTENDED, argument, SDIO_RESPONSE_SHORT, response) != HAL_SUCCESS)
    {
        sdio_lld_data_error_cleanup(sdiop);
        return HAL_FAILED;
    }

    // for writes the data path is enabled after the response
    if (direction == SDIO_DIRECTION_WRITE)
    {
        sdiop->sdio->DCTRL = dctrl;
    }

    // wait for the end of the transfer
    // note the mask is checked before going to sleep because the interrupt may have occurred already
    osalSysLock();

    if ((sdiop->sdio->MASK & SDIO_NF_MASK_DATA) != 0U)
    {
        msg = osalThreadSuspendTimeoutS(&sdiop->thread, OSAL_MS2I(timeoutMs + 10U));
    }

    osalSysUnlock();

    if (msg == MSG_TIMEOUT)
    {
        sdiop->errors |= SDIO_TRANSFER_TIMEOUT;
        sdio_lld_data_error_cleanup(sdiop);
        return HAL_FAILED;
    }

    if ((sdiop->sdio->STA & SDIO_STA_DATAEND) == 0U)
    {
        sdio_lld_data_error_cleanup(sdiop);
        return HAL_FAILED;
    }

    // wait for transfer completion at DMA level, the stream is then disabled and cleared
    dmaWaitCompletion(sdiop->dma);

    sdiop->sdio->DCTRL = sdio_lld_idle_dctrl(sdiop);
    sdiop->sdio->ICR = SDIO_NF_STA_DATA_FLAGS;

    return HAL_SUCCESS;
}

void sdio_lld_enable_card_interrupt(SDIODriver *sdiop, bool enable)
{
    osalSysLock();

    if (enable)
    {
        sdiop->sdio->MASK |= SDIO_MASK_SDIOITIE;

        // the card interrupt is only detected with SDIOEN set (no effect on the data path while DTEN is clear)
        if ((sdiop->sdio->DCTRL & SDIO_DCTRL_DTEN) == 0U)
        {
            sdiop->sdio->DCTRL = SDIO_DCTRL_SDIOEN;
        }
    }
    else
    {
        sdiop->sdio->MASK &= ~SDIO_MASK_SDIOITIE;
        sdiop->sdio->ICR = SDIO_ICR_SDIOITC;
    }

    osalSysUnlock();
}

#endif // HAL_NF_USE_STM32_SDIO
