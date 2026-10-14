//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// ChibiOS hardware types for the Cypress/Infineon HAL API (cyhal_sdio.h, cyhal_gpio.h) used by WHD.
// Replaces the placeholder cyhal_hw_types.h shipped with WHD.

#ifndef CYHAL_HW_TYPES_H
#define CYHAL_HW_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // GPIO pin: a ChibiOS PAL line (ioline_t)
    typedef uint32_t cyhal_gpio_t;

    typedef struct
    {
        void *div_type;
    } cyhal_clock_divider_t;

    // SDIO host: the STM32_SDIO overlay driver
    typedef struct
    {
        // SDIODriver instance
        void *driver;
        // card interrupt handler registered by WHD (cyhal_sdio_irq_handler_t)
        void *irqHandler;
        void *irqHandlerArg;
        // card interrupt requested by WHD
        volatile bool irqEnabled;
    } cyhal_sdio_t;

    typedef struct
    {
        void *empty;
    } cyhal_spi_t;

    typedef struct
    {
        void *empty;
    } cyhal_m2m_t;

#ifdef __cplusplus
}
#endif

#endif // CYHAL_HW_TYPES_H
