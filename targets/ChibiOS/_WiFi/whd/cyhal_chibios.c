//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// ChibiOS implementation of the Cypress/Infineon HAL API subset used by WHD (v1 API):
// - SDIO: on top of the STM32_SDIO nF overlay driver
// - GPIO: on top of PAL (out-of-band WLAN interrupt)

#include <string.h>
#include <hal.h>
#include <hal_nf_community.h>
#include <cyhal_sdio.h>
#include <cyhal_gpio.h>

#if (HAL_NF_USE_STM32_SDIO != TRUE)
#error "WHD on ChibiOS requires the STM32_SDIO overlay driver (HAL_NF_USE_STM32_SDIO)"
#endif

// block size used by WHD for CMD53 block mode transfers (SDIO_64B_BLOCK)
#define CYHAL_SDIO_DEFAULT_BLOCK_SIZE 64U

// bounce buffer for transfers that can't be done in place:
// unaligned buffers, block mode transfers with a length that isn't a multiple of the block size
// and byte mode transfers rounded up to a power of 2
#define CYHAL_SDIO_BOUNCE_BUFFER_SIZE 2048U

// card enumeration retries (1ms apart)
#define CYHAL_SDIO_ENUMERATION_RETRIES 500U

static uint32_t bounceBuffer[CYHAL_SDIO_BOUNCE_BUFFER_SIZE / sizeof(uint32_t)];

static uint32_t sdioBlockSize = CYHAL_SDIO_DEFAULT_BLOCK_SIZE;

// the SDIO object registered for the card interrupt (single SDIO host)
static cyhal_sdio_t *cardInterruptObject;

//////////
// SDIO //
//////////

static cy_rslt_t cyhal_sdio_error(uint32_t errors)
{
    uint32_t code = CYHAL_SDIO_RET_NO_SP_ERRORS;

    if (errors & SDIO_CMD_CRC_ERROR)
    {
        code = CYHAL_SDIO_RET_CMD_CRC_ERROR;
    }
    else if (errors & SDIO_CMD_TIMEOUT)
    {
        code = CYHAL_SDIO_RET_CMD_TIMEOUT;
    }
    else if (errors & SDIO_DATA_CRC_ERROR)
    {
        code = CYHAL_SDIO_RET_DAT_CRC_ERROR;
    }
    else if (errors & (SDIO_DATA_TIMEOUT | SDIO_TRANSFER_TIMEOUT))
    {
        code = CYHAL_SDIO_RET_DAT_TIMEOUT;
    }
    else if (errors & SDIO_RESPONSE_ERROR)
    {
        code = CYHAL_SDIO_RET_RESP_FLAG_ERROR;
    }

    return CYHAL_SDIO_RSLT_ERR_FUNC_RET(code);
}

// called from ISR context by the SDIO driver (the card interrupt is one-shot)
static void cyhal_sdio_card_interrupt(SDIODriver *sdiop)
{
    (void)sdiop;

    cyhal_sdio_t *obj = cardInterruptObject;

    if (obj != NULL && obj->irqHandler != NULL)
    {
        ((cyhal_sdio_irq_handler_t)obj->irqHandler)(obj->irqHandlerArg, CYHAL_SDIO_CARD_INTERRUPT);
    }
}

// WHD expects the card interrupt to stay enabled; the SDIO driver disables it when it fires.
// Re-arm it after each bus transaction (the WHD thread always talks to the card after an interrupt).
static void cyhal_sdio_rearm_card_interrupt(const cyhal_sdio_t *obj)
{
    if (obj->irqEnabled)
    {
        sdioEnableCardInterrupt((SDIODriver *)obj->driver, cyhal_sdio_card_interrupt);
    }
}

static uint32_t cyhal_sdio_round_up_power_of_2(uint32_t value)
{
    uint32_t result = 1;

    while (result < value)
    {
        result <<= 1;
    }

    return result;
}

// Starts the SDIO host and enumerates the card (CMD0, CMD5, CMD3, CMD7).
// Leaves the host in 4 bit mode at 400kHz, WHD switches the card to 4 bit (CCCR) before any data transfer
// and cyhal_sdio_configure() sets the bus clock.
// The pins are configured in board.h, the arguments are ignored.
cy_rslt_t cyhal_sdio_init(
    cyhal_sdio_t *obj,
    cyhal_gpio_t cmd,
    cyhal_gpio_t clk,
    cyhal_gpio_t data0,
    cyhal_gpio_t data1,
    cyhal_gpio_t data2,
    cyhal_gpio_t data3)
{
    SDIODriver *sdiop;
    uint32_t response = 0;
    uint32_t i;

    (void)cmd;
    (void)clk;
    (void)data0;
    (void)data1;
    (void)data2;
    (void)data3;

    if (obj == NULL || obj->driver == NULL)
    {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    sdiop = (SDIODriver *)obj->driver;

    obj->irqHandler = NULL;
    obj->irqHandlerArg = NULL;
    obj->irqEnabled = false;

    sdioStart(sdiop, NULL);

    for (i = 0; i < CYHAL_SDIO_ENUMERATION_RETRIES; i++)
    {
        sdioSendCmd(sdiop, SDIO_CMD_GO_IDLE_STATE, 0, SDIO_RESPONSE_NONE, NULL);
        (void)sdioSendCmd(sdiop, SDIO_CMD_IO_SEND_OP_COND, 0, SDIO_RESPONSE_SHORT_NO_CRC, &response);

        if (sdioSendCmd(sdiop, SDIO_CMD_SEND_RELATIVE_ADDR, 0, SDIO_RESPONSE_SHORT, &response) == HAL_SUCCESS)
        {
            break;
        }

        chThdSleepMilliseconds(1);
    }

    // clear the errors from the enumeration retries
    (void)sdioGetAndClearErrors(sdiop);

    if (i == CYHAL_SDIO_ENUMERATION_RETRIES)
    {
        return CYHAL_SDIO_RSLT_ERR_FUNC_RET(CYHAL_SDIO_RET_CMD_TIMEOUT);
    }

    // select the card with the RCA from the CMD3 response
    if (sdioSendCmd(sdiop, SDIO_CMD_SELECT_DESELECT, response & 0xFFFF0000U, SDIO_RESPONSE_SHORT, &response) !=
        HAL_SUCCESS)
    {
        return cyhal_sdio_error(sdioGetAndClearErrors(sdiop));
    }

    sdioSetBusWidth(sdiop, SDIO_BUS_WIDTH_4BIT);

    return CY_RSLT_SUCCESS;
}

void cyhal_sdio_free(cyhal_sdio_t *obj)
{
    if (obj == NULL || obj->driver == NULL)
    {
        return;
    }

    sdioDisableCardInterrupt((SDIODriver *)obj->driver);
    sdioStop((SDIODriver *)obj->driver);

    if (cardInterruptObject == obj)
    {
        cardInterruptObject = NULL;
    }
}

cy_rslt_t cyhal_sdio_configure(cyhal_sdio_t *obj, const cyhal_sdio_cfg_t *config)
{
    if (obj == NULL || obj->driver == NULL || config == NULL)
    {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    if (config->frequencyhal_hz != 0)
    {
        sdioSetClock((SDIODriver *)obj->driver, config->frequencyhal_hz);
    }

    if (config->block_size != 0)
    {
        sdioBlockSize = config->block_size;
    }

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cyhal_sdio_send_cmd(
    const cyhal_sdio_t *obj,
    cyhal_transfer_t direction,
    cyhal_sdio_command_t command,
    uint32_t argument,
    uint32_t *response)
{
    SDIODriver *sdiop;
    sdioresponse_t responseType;
    uint32_t r = 0;
    bool result;

    (void)direction;

    if (obj == NULL || obj->driver == NULL)
    {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    sdiop = (SDIODriver *)obj->driver;

    switch (command)
    {
        case CYHAL_SDIO_CMD_GO_IDLE_STATE:
            responseType = SDIO_RESPONSE_NONE;
            break;

        case CYHAL_SDIO_CMD_IO_SEND_OP_COND:
            responseType = SDIO_RESPONSE_SHORT_NO_CRC;
            break;

        default:
            responseType = SDIO_RESPONSE_SHORT;
            break;
    }

    sdioAcquireBus(sdiop);

    result = sdioSendCmd(sdiop, (uint8_t)command, argument, responseType, &r);

    if (result == HAL_SUCCESS && command == CYHAL_SDIO_CMD_IO_RW_DIRECT && (r & SDIO_R5_ERROR_MASK) != 0U)
    {
        result = HAL_FAILED;
        sdiop->errors |= SDIO_RESPONSE_ERROR;
    }

    cyhal_sdio_rearm_card_interrupt(obj);

    sdioReleaseBus(sdiop);

    if (response != NULL)
    {
        *response = r;
    }

    if (result != HAL_SUCCESS)
    {
        return cyhal_sdio_error(sdioGetAndClearErrors(sdiop));
    }

    return CY_RSLT_SUCCESS;
}

// CMD53 transfer with a WHD encoded argument.
cy_rslt_t cyhal_sdio_bulk_transfer(
    cyhal_sdio_t *obj,
    cyhal_transfer_t direction,
    uint32_t argument,
    const uint32_t *data,
    uint16_t length,
    uint32_t *response)
{
    SDIODriver *sdiop;
    sdiodirection_t sdioDirection = (direction == CYHAL_WRITE) ? SDIO_DIRECTION_WRITE : SDIO_DIRECTION_READ;
    uint32_t count = SDIO_ARG_CMD53_COUNT(argument);
    uint32_t transferLength;
    uint32_t blockSize;
    uint8_t *buffer = (uint8_t *)data;
    bool bounce = (((uint32_t)data) & 3U) != 0U;
    uint32_t r5 = 0;
    bool result;

    if (obj == NULL || obj->driver == NULL || data == NULL || length == 0)
    {
        return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
    }

    sdiop = (SDIODriver *)obj->driver;

    if ((argument & SDIO_ARG_CMD53_BLOCK_MODE) != 0U)
    {
        if (count == 0)
        {
            return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
        }

        // the card transfers whole blocks, WHD may ask for less (the count is rounded up)
        blockSize = sdioBlockSize;
        transferLength = count * blockSize;
    }
    else
    {
        // a count of 0 means 512 bytes
        if (count == 0)
        {
            count = SDIO_MAX_BYTE_MODE_COUNT;
        }

        // byte mode: round the count up to a power of 2 so the data path runs as a single block
        // (same as the WICED STM32 driver, proven with these WLAN chips)
        transferLength = cyhal_sdio_round_up_power_of_2(count);
        argument = (argument & ~0x1FFU) | SDIO_ARG_CMD53_COUNT(transferLength);
        blockSize = 0;
    }

    if (transferLength != length)
    {
        bounce = true;
    }

    if (bounce)
    {
        if (transferLength > sizeof(bounceBuffer))
        {
            return CYHAL_SDIO_RSLT_ERR_BAD_PARAM;
        }

        if (sdioDirection == SDIO_DIRECTION_WRITE)
        {
            uint32_t copyLength = (length < transferLength) ? length : transferLength;

            memcpy(bounceBuffer, data, copyLength);
            memset((uint8_t *)bounceBuffer + copyLength, 0, sizeof(bounceBuffer) - copyLength);
        }

        buffer = (uint8_t *)bounceBuffer;
    }

    sdioAcquireBus(sdiop);

    result = sdioTransfer(sdiop, sdioDirection, argument, buffer, transferLength, blockSize, &r5);

    if (result == HAL_SUCCESS && (r5 & SDIO_R5_ERROR_MASK) != 0U)
    {
        result = HAL_FAILED;
        sdiop->errors |= SDIO_RESPONSE_ERROR;
    }

    if (result == HAL_SUCCESS && bounce && sdioDirection == SDIO_DIRECTION_READ)
    {
        memcpy((void *)data, bounceBuffer, (length < transferLength) ? length : transferLength);
    }

    cyhal_sdio_rearm_card_interrupt(obj);

    sdioReleaseBus(sdiop);

    if (response != NULL)
    {
        *response = r5;
    }

    if (result != HAL_SUCCESS)
    {
        return cyhal_sdio_error(sdioGetAndClearErrors(sdiop));
    }

    return CY_RSLT_SUCCESS;
}

void cyhal_sdio_register_irq(cyhal_sdio_t *obj, cyhal_sdio_irq_handler_t handler, void *handler_arg)
{
    if (obj == NULL)
    {
        return;
    }

    obj->irqHandler = (void *)handler;
    obj->irqHandlerArg = handler_arg;

    cardInterruptObject = obj;
}

void cyhal_sdio_irq_enable(cyhal_sdio_t *obj, cyhal_sdio_irq_event_t event, bool enable)
{
    if (obj == NULL || obj->driver == NULL || (event & CYHAL_SDIO_CARD_INTERRUPT) == 0)
    {
        return;
    }

    obj->irqEnabled = enable;

    if (enable)
    {
        sdioEnableCardInterrupt((SDIODriver *)obj->driver, cyhal_sdio_card_interrupt);
    }
    else
    {
        sdioDisableCardInterrupt((SDIODriver *)obj->driver);
    }
}

//////////
// GPIO //
//////////

// one interrupt capable pin (the WLAN out-of-band interrupt)
typedef struct
{
    cyhal_gpio_t pin;
    cyhal_gpio_irq_handler_t handler;
    void *handlerArg;
    cyhal_gpio_irq_event_t event;
} cyhal_gpio_irq_slot_t;

static cyhal_gpio_irq_slot_t gpioIrqSlot = {CYHAL_NC_PIN_VALUE, NULL, NULL, CYHAL_GPIO_IRQ_NONE};

// called from ISR context by PAL
static void cyhal_gpio_pal_callback(void *arg)
{
    cyhal_gpio_irq_slot_t *slot = (cyhal_gpio_irq_slot_t *)arg;

    if (slot->handler != NULL)
    {
        slot->handler(slot->handlerArg, slot->event);
    }
}

cy_rslt_t cyhal_gpio_init(cyhal_gpio_t pin, cyhal_gpio_direction_t direction, cyhal_gpio_drive_mode_t drvMode, bool initVal)
{
    iomode_t mode;

    if (pin == CYHAL_NC_PIN_VALUE)
    {
        return CY_RSLT_SUCCESS;
    }

    if (direction == CYHAL_GPIO_DIR_INPUT)
    {
        switch (drvMode)
        {
            case CYHAL_GPIO_DRIVE_PULLUP:
                mode = PAL_MODE_INPUT_PULLUP;
                break;

            case CYHAL_GPIO_DRIVE_PULLDOWN:
                mode = PAL_MODE_INPUT_PULLDOWN;
                break;

            default:
                mode = PAL_MODE_INPUT;
                break;
        }
    }
    else
    {
        mode = (drvMode == CYHAL_GPIO_DRIVE_OPENDRAINDRIVESLOW) ? PAL_MODE_OUTPUT_OPENDRAIN
                                                                 : PAL_MODE_OUTPUT_PUSHPULL;

        palWriteLine((ioline_t)pin, initVal ? PAL_HIGH : PAL_LOW);
    }

    palSetLineMode((ioline_t)pin, mode);

    return CY_RSLT_SUCCESS;
}

void cyhal_gpio_free(cyhal_gpio_t pin)
{
    if (pin == CYHAL_NC_PIN_VALUE)
    {
        return;
    }

    if (gpioIrqSlot.pin == pin)
    {
        palDisableLineEvent((ioline_t)pin);
    }
}

void cyhal_gpio_write(cyhal_gpio_t pin, bool value)
{
    if (pin != CYHAL_NC_PIN_VALUE)
    {
        palWriteLine((ioline_t)pin, value ? PAL_HIGH : PAL_LOW);
    }
}

bool cyhal_gpio_read(cyhal_gpio_t pin)
{
    if (pin == CYHAL_NC_PIN_VALUE)
    {
        return false;
    }

    return palReadLine((ioline_t)pin) == PAL_HIGH;
}

void cyhal_gpio_register_irq(cyhal_gpio_t pin, uint8_t intrPriority, cyhal_gpio_irq_handler_t handler, void *handler_arg)
{
    (void)intrPriority;

    if (pin == CYHAL_NC_PIN_VALUE)
    {
        return;
    }

    gpioIrqSlot.pin = pin;
    gpioIrqSlot.handler = handler;
    gpioIrqSlot.handlerArg = handler_arg;
}

void cyhal_gpio_irq_enable(cyhal_gpio_t pin, cyhal_gpio_irq_event_t event, bool enable)
{
    ioeventmode_t mode;

    if (pin == CYHAL_NC_PIN_VALUE || gpioIrqSlot.pin != pin)
    {
        return;
    }

    if (!enable || event == CYHAL_GPIO_IRQ_NONE)
    {
        palDisableLineEvent((ioline_t)pin);
        return;
    }

    switch (event)
    {
        case CYHAL_GPIO_IRQ_RISE:
            mode = PAL_EVENT_MODE_RISING_EDGE;
            break;

        case CYHAL_GPIO_IRQ_FALL:
            mode = PAL_EVENT_MODE_FALLING_EDGE;
            break;

        default:
            mode = PAL_EVENT_MODE_BOTH_EDGES;
            break;
    }

    gpioIrqSlot.event = event;

    palEnableLineEvent((ioline_t)pin, mode);
    palSetLineCallback((ioline_t)pin, cyhal_gpio_pal_callback, &gpioIrqSlot);
}
