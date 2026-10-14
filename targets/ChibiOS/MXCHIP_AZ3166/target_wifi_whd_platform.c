//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// WLAN platform definition for the MXCHIP AZ3166 (EMW3166 module, BCM43362 on SDIO) for the WHD backend.

#include <hal.h>
#include <hal_nf_community.h>
#include <cyhal_gpio.h>
#include <nf_wifi_whd.h>

// SDIO bus clock
#define WIFI_SDIO_CLOCK 25000000U

// from common/43362A2_bin.c and common/43362_nvram.c
extern const uint8_t g_wifiFirmware43362[];
extern const uint32_t g_wifiFirmware43362Size;
extern const char g_wifiNvram43362[];
extern const uint32_t g_wifiNvram43362Size;

static void WifiPowerUp(void)
{
    // bootstrap pins not driven (as the Azure RTOS WICED library does)
    palSetLineMode(LINE_WWD_BOOTSTRAP_0, PAL_MODE_INPUT);
    palSetLineMode(LINE_WWD_BOOTSTRAP_1, PAL_MODE_INPUT);

    // reset pulse (the 32kHz sleep clock is on MCO1 from the LSE)
    palClearLine(LINE_WWD_PIN_RESET);
    chThdSleepMilliseconds(10);
    palSetLine(LINE_WWD_PIN_RESET);
    chThdSleepMilliseconds(10);
}

static void WifiPowerDown(void)
{
    palClearLine(LINE_WWD_PIN_RESET);
}

const nf_wifi_whd_platform_t g_wifiWhdPlatform = {
    .powerUp = WifiPowerUp,
    .powerDown = WifiPowerDown,
    .sdioDriver = &SDIOD1,
    .sdioClock = WIFI_SDIO_CLOCK,
    .oobPin = (cyhal_gpio_t)LINE_SDIO_OOB_IRQ,
    .oobFallingEdge = false,
    .firmware = {g_wifiFirmware43362, &g_wifiFirmware43362Size},
    .nvram = {(const uint8_t *)g_wifiNvram43362, &g_wifiNvram43362Size},
};
