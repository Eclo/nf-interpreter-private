//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#ifndef HAL_NF_STM32_SDIO_H
#define HAL_NF_STM32_SDIO_H

#if (HAL_NF_USE_STM32_SDIO == TRUE)

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

// SD I/O commands
#define SDIO_CMD_GO_IDLE_STATE      0U
#define SDIO_CMD_SEND_RELATIVE_ADDR 3U
#define SDIO_CMD_IO_SEND_OP_COND    5U
#define SDIO_CMD_SELECT_DESELECT    7U
#define SDIO_CMD_IO_RW_DIRECT       52U
#define SDIO_CMD_IO_RW_EXTENDED     53U

// CMD52/CMD53 argument fields
#define SDIO_ARG_WRITE              (1UL << 31)
#define SDIO_ARG_FUNCTION(fn)       (((uint32_t)(fn) & 0x7U) << 28)
#define SDIO_ARG_CMD52_RAW          (1UL << 27)
#define SDIO_ARG_CMD53_BLOCK_MODE   (1UL << 27)
#define SDIO_ARG_CMD53_INCREMENT    (1UL << 26)
#define SDIO_ARG_ADDRESS(address)   (((uint32_t)(address) & 0x1FFFFU) << 9)
#define SDIO_ARG_CMD52_DATA(data)   ((uint32_t)(data) & 0xFFU)
#define SDIO_ARG_CMD53_COUNT(count) ((uint32_t)(count) & 0x1FFU)

// R5 response (CMD52/CMD53) flags
#define SDIO_R5_COM_CRC_ERROR   (1UL << 15)
#define SDIO_R5_ILLEGAL_COMMAND (1UL << 14)
#define SDIO_R5_ERROR           (1UL << 11)
#define SDIO_R5_FUNCTION_NUMBER (1UL << 9)
#define SDIO_R5_OUT_OF_RANGE    (1UL << 8)
#define SDIO_R5_ERROR_MASK                                                                                             \
    (SDIO_R5_COM_CRC_ERROR | SDIO_R5_ILLEGAL_COMMAND | SDIO_R5_ERROR | SDIO_R5_FUNCTION_NUMBER | SDIO_R5_OUT_OF_RANGE)

// limits for CMD53 transfers
#define SDIO_MAX_BYTE_MODE_COUNT  512U
#define SDIO_MAX_BLOCK_MODE_COUNT 511U
#define SDIO_MAX_BLOCK_SIZE       2048U

// error flags
#define SDIO_NO_ERROR         0U
#define SDIO_CMD_CRC_ERROR    (1U << 0)
#define SDIO_CMD_TIMEOUT      (1U << 1)
#define SDIO_DATA_CRC_ERROR   (1U << 2)
#define SDIO_DATA_TIMEOUT     (1U << 3)
#define SDIO_TX_UNDERRUN      (1U << 4)
#define SDIO_RX_OVERRUN       (1U << 5)
#define SDIO_STARTBIT_ERROR   (1U << 6)
#define SDIO_RESPONSE_ERROR   (1U << 7)
#define SDIO_TRANSFER_TIMEOUT (1U << 8)

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

// Enables the sdioAcquireBus() and sdioReleaseBus() APIs.
#if !defined(SDIO_USE_MUTUAL_EXCLUSION)
#define SDIO_USE_MUTUAL_EXCLUSION TRUE
#endif

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

// this driver and the ChibiOS SDC driver both own the SDIO peripheral and its IRQ handler
#if (HAL_USE_SDC == TRUE)
#error "HAL_NF_USE_STM32_SDIO and HAL_USE_SDC can't be enabled at the same time"
#endif

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

typedef enum
{
    SDIO_UNINIT = 0,
    SDIO_STOP = 1,
    SDIO_READY = 2,
} sdiostate_t;

typedef enum
{
    // no response expected
    SDIO_RESPONSE_NONE = 0,
    // short response with CRC (R1, R5, R6)
    SDIO_RESPONSE_SHORT,
    // short response without valid CRC (R3, R4)
    SDIO_RESPONSE_SHORT_NO_CRC,
    // long response (R2)
    SDIO_RESPONSE_LONG,
} sdioresponse_t;

typedef enum
{
    SDIO_DIRECTION_READ = 0,
    SDIO_DIRECTION_WRITE,
} sdiodirection_t;

typedef enum
{
    SDIO_BUS_WIDTH_1BIT = 0,
    SDIO_BUS_WIDTH_4BIT,
} sdiobuswidth_t;

typedef struct SDIODriver SDIODriver;

// Card interrupt callback, called from ISR context.
// The card interrupt is disabled before the callback is called, call sdioEnableCardInterrupt() to re-arm it.
typedef void (*sdiocallback_t)(SDIODriver *sdiop);

typedef struct
{
    // data transfer timeout in milliseconds (0 to use STM32_SDIO_NF_DATA_TIMEOUT_MS)
    uint32_t dataTimeoutMs;
} SDIOConfig;

#include "sdio_lld.h"

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C"
{
#endif

    void sdioInit(void);
    void sdioObjectInit(SDIODriver *sdiop);
    void sdioStart(SDIODriver *sdiop, const SDIOConfig *config);
    void sdioStop(SDIODriver *sdiop);

    void sdioSetClock(SDIODriver *sdiop, uint32_t frequency);
    void sdioSetBusWidth(SDIODriver *sdiop, sdiobuswidth_t width);

    bool sdioSendCmd(
        SDIODriver *sdiop,
        uint8_t command,
        uint32_t argument,
        sdioresponse_t responseType,
        uint32_t *response);

    bool sdioCmd52(
        SDIODriver *sdiop,
        sdiodirection_t direction,
        uint8_t function,
        uint32_t address,
        uint8_t value,
        uint8_t *response);

    bool sdioCmd53(
        SDIODriver *sdiop,
        sdiodirection_t direction,
        uint8_t function,
        uint32_t address,
        bool incrementAddress,
        uint8_t *buffer,
        uint32_t length,
        uint32_t blockSize);

    bool sdioTransfer(
        SDIODriver *sdiop,
        sdiodirection_t direction,
        uint32_t argument,
        uint8_t *buffer,
        uint32_t length,
        uint32_t blockSize,
        uint32_t *response);

    void sdioEnableCardInterrupt(SDIODriver *sdiop, sdiocallback_t callback);
    void sdioDisableCardInterrupt(SDIODriver *sdiop);

    uint32_t sdioGetAndClearErrors(SDIODriver *sdiop);

#if (SDIO_USE_MUTUAL_EXCLUSION == TRUE)
    void sdioAcquireBus(SDIODriver *sdiop);
    void sdioReleaseBus(SDIODriver *sdiop);
#endif

#ifdef __cplusplus
}
#endif

#endif // HAL_NF_USE_STM32_SDIO

#endif // HAL_NF_STM32_SDIO_H
