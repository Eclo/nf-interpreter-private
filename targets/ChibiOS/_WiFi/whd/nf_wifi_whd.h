//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// Contract between the WHD (Infineon Wi-Fi Host Driver) backend and the target.
// The target provides g_wifiWhdPlatform with the board specific details.

#ifndef NF_WIFI_WHD_H
#define NF_WIFI_WHD_H

#include <stdint.h>
#include <stdbool.h>
#include <cyhal_hw_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        const uint8_t *data;
        // pointer to the size (the images usually come from other translation units)
        const uint32_t *size;
    } nf_wifi_whd_resource_t;

    typedef struct
    {
        // powers up the WLAN chip in SDIO mode (bootstrap pins, reset/power sequence)
        void (*powerUp)(void);
        // powers down the WLAN chip
        void (*powerDown)(void);

        // SDIO host driver (SDIODriver from the STM32_SDIO overlay)
        void *sdioDriver;
        // SDIO bus clock (Hz)
        uint32_t sdioClock;

        // host-wake out-of-band interrupt pin (PAL line) or CYHAL_NC_PIN_VALUE to use the in-band SDIO interrupt
        cyhal_gpio_t oobPin;
        // out-of-band interrupt on falling edge (otherwise rising edge)
        bool oobFallingEdge;

        // WLAN firmware and NVRAM images (memory mapped)
        nf_wifi_whd_resource_t firmware;
        nf_wifi_whd_resource_t nvram;
    } nf_wifi_whd_platform_t;

    // provided by the target
    extern const nf_wifi_whd_platform_t g_wifiWhdPlatform;

#ifdef __cplusplus
}
#endif

#endif // NF_WIFI_WHD_H
