//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

/*
 * Hardware Abstraction Layer for SD I/O cards (SDIO host)
 */
#include <hal.h>
#include <hal_nf_community.h>

#if (HAL_NF_USE_STM32_SDIO == TRUE)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

static bool sdio_is_power_of_2(uint32_t value)
{
    return (value != 0U) && ((value & (value - 1U)) == 0U);
}

// Checks the R5 response flags of a CMD52/CMD53.
static bool sdio_check_r5(SDIODriver *sdiop, uint32_t response)
{
    if ((response & SDIO_R5_ERROR_MASK) != 0U)
    {
        sdiop->errors |= SDIO_RESPONSE_ERROR;
        return HAL_FAILED;
    }

    return HAL_SUCCESS;
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

// Initializes the SDIO driver
void sdioInit(void)
{
    sdio_lld_init();
}

// Initializes the standard part of a SDIODriver structure
void sdioObjectInit(SDIODriver *sdiop)
{
    sdiop->state = SDIO_STOP;
    sdiop->config = NULL;
    sdiop->errors = SDIO_NO_ERROR;
    sdiop->cardInterruptCallback = NULL;

#if (SDIO_USE_MUTUAL_EXCLUSION == TRUE)
    osalMutexObjectInit(&sdiop->mutex);
#endif
}

// Configures and activates the SDIO peripheral.
// The bus is powered and clocked in identification mode: 400kHz, 1 bit bus width.
void sdioStart(SDIODriver *sdiop, const SDIOConfig *config)
{
    osalDbgCheck(sdiop != NULL);

    osalSysLock();
    osalDbgAssert((sdiop->state == SDIO_STOP) || (sdiop->state == SDIO_READY), "invalid state");

    sdiop->config = config;
    sdio_lld_start(sdiop);
    osalSysUnlock();

    sdio_lld_start_clock(sdiop);

    sdiop->errors = SDIO_NO_ERROR;
    sdiop->state = SDIO_READY;
}

// Deactivates the SDIO peripheral
void sdioStop(SDIODriver *sdiop)
{
    osalDbgCheck(sdiop != NULL);

    osalSysLock();
    osalDbgAssert((sdiop->state == SDIO_STOP) || (sdiop->state == SDIO_READY), "invalid state");

    sdio_lld_stop(sdiop);
    sdiop->config = NULL;
    sdiop->cardInterruptCallback = NULL;
    sdiop->state = SDIO_STOP;
    osalSysUnlock();
}

// Sets the bus clock frequency (capped to STM32_SDIO_NF_MAX_CLOCK)
void sdioSetClock(SDIODriver *sdiop, uint32_t frequency)
{
    osalDbgCheck((sdiop != NULL) && (frequency > 0U));
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    sdio_lld_set_clock(sdiop, frequency);
}

// Sets the host bus width.
// The card has to be switched first (CCCR bus interface control register).
void sdioSetBusWidth(SDIODriver *sdiop, sdiobuswidth_t width)
{
    osalDbgCheck(sdiop != NULL);
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    sdio_lld_set_bus_width(sdiop, width);
}

// Sends a command without data (e.g. CMD0, CMD3, CMD5, CMD7, CMD52).
bool sdioSendCmd(SDIODriver *sdiop, uint8_t command, uint32_t argument, sdioresponse_t responseType, uint32_t *response)
{
    osalDbgCheck(sdiop != NULL);
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    return sdio_lld_send_cmd(sdiop, command, argument, responseType, response);
}

// Single register read or write (CMD52, IO_RW_DIRECT).
// For reads the register value is returned in response, for writes response is optional.
bool sdioCmd52(
    SDIODriver *sdiop,
    sdiodirection_t direction,
    uint8_t function,
    uint32_t address,
    uint8_t value,
    uint8_t *response)
{
    uint32_t argument = SDIO_ARG_FUNCTION(function) | SDIO_ARG_ADDRESS(address);
    uint32_t r5;

    osalDbgCheck((sdiop != NULL) && (function <= 7U) && (address <= 0x1FFFFU));
    osalDbgCheck((direction == SDIO_DIRECTION_WRITE) || (response != NULL));
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    if (direction == SDIO_DIRECTION_WRITE)
    {
        argument |= SDIO_ARG_WRITE | SDIO_ARG_CMD52_DATA(value);
    }

    if (sdio_lld_send_cmd(sdiop, SDIO_CMD_IO_RW_DIRECT, argument, SDIO_RESPONSE_SHORT, &r5) != HAL_SUCCESS)
    {
        return HAL_FAILED;
    }

    if (sdio_check_r5(sdiop, r5) != HAL_SUCCESS)
    {
        return HAL_FAILED;
    }

    if (response != NULL)
    {
        *response = (uint8_t)(r5 & 0xFFU);
    }

    return HAL_SUCCESS;
}

// Multiple byte or block transfer (CMD53, IO_RW_EXTENDED).
// blockSize == 0 selects byte mode (length 1..512), otherwise block mode with a power of 2 block size
// (length a multiple of blockSize, up to 511 blocks).
// The buffer has to be 32 bits aligned and, for reads, its size rounded up to a multiple of 4 bytes.
bool sdioCmd53(
    SDIODriver *sdiop,
    sdiodirection_t direction,
    uint8_t function,
    uint32_t address,
    bool incrementAddress,
    uint8_t *buffer,
    uint32_t length,
    uint32_t blockSize)
{
    uint32_t argument = SDIO_ARG_FUNCTION(function) | SDIO_ARG_ADDRESS(address);
    uint32_t r5;

    osalDbgCheck((sdiop != NULL) && (function <= 7U) && (address <= 0x1FFFFU) && (buffer != NULL));

    if (direction == SDIO_DIRECTION_WRITE)
    {
        argument |= SDIO_ARG_WRITE;
    }

    if (incrementAddress)
    {
        argument |= SDIO_ARG_CMD53_INCREMENT;
    }

    if (blockSize == 0U)
    {
        // byte mode, a count of 0 means 512 bytes
        if ((length == 0U) || (length > SDIO_MAX_BYTE_MODE_COUNT))
        {
            return HAL_FAILED;
        }

        argument |= SDIO_ARG_CMD53_COUNT(length);
    }
    else
    {
        if (!sdio_is_power_of_2(blockSize) || (blockSize > SDIO_MAX_BLOCK_SIZE) || (length == 0U) ||
            ((length % blockSize) != 0U) || ((length / blockSize) > SDIO_MAX_BLOCK_MODE_COUNT))
        {
            return HAL_FAILED;
        }

        argument |= SDIO_ARG_CMD53_BLOCK_MODE | SDIO_ARG_CMD53_COUNT(length / blockSize);
    }

    if (sdioTransfer(sdiop, direction, argument, buffer, length, blockSize, &r5) != HAL_SUCCESS)
    {
        return HAL_FAILED;
    }

    return sdio_check_r5(sdiop, r5);
}

// Raw CMD53 transfer with a caller encoded argument.
// The R5 response is returned in response (optional) and is not checked.
// blockSize and length have to be consistent with the block mode and count fields in the argument.
bool sdioTransfer(
    SDIODriver *sdiop,
    sdiodirection_t direction,
    uint32_t argument,
    uint8_t *buffer,
    uint32_t length,
    uint32_t blockSize,
    uint32_t *response)
{
    osalDbgCheck((sdiop != NULL) && (buffer != NULL) && (length > 0U));
    osalDbgCheck((blockSize == 0U) || sdio_is_power_of_2(blockSize));
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    return sdio_lld_transfer(sdiop, direction, argument, buffer, length, blockSize, response);
}

// Enables the card interrupt.
// The callback is called from ISR context, after which the card interrupt is disabled.
// Call this again to re-arm it.
void sdioEnableCardInterrupt(SDIODriver *sdiop, sdiocallback_t callback)
{
    osalDbgCheck((sdiop != NULL) && (callback != NULL));
    osalDbgAssert(sdiop->state == SDIO_READY, "invalid state");

    sdiop->cardInterruptCallback = callback;
    sdio_lld_enable_card_interrupt(sdiop, true);
}

void sdioDisableCardInterrupt(SDIODriver *sdiop)
{
    osalDbgCheck(sdiop != NULL);

    sdio_lld_enable_card_interrupt(sdiop, false);
}

// Returns the accumulated errors (SDIO_xxx_ERROR flags) and clears them
uint32_t sdioGetAndClearErrors(SDIODriver *sdiop)
{
    uint32_t errors;

    osalDbgCheck(sdiop != NULL);

    osalSysLock();
    errors = sdiop->errors;
    sdiop->errors = SDIO_NO_ERROR;
    osalSysUnlock();

    return errors;
}

#if (SDIO_USE_MUTUAL_EXCLUSION == TRUE)

// Gains exclusive access to the SDIO bus
void sdioAcquireBus(SDIODriver *sdiop)
{
    osalDbgCheck(sdiop != NULL);

    osalMutexLock(&sdiop->mutex);
}

// Releases exclusive access to the SDIO bus
void sdioReleaseBus(SDIODriver *sdiop)
{
    osalDbgCheck(sdiop != NULL);

    osalMutexUnlock(&sdiop->mutex);
}

#endif // SDIO_USE_MUTUAL_EXCLUSION

#endif // HAL_NF_USE_STM32_SDIO
