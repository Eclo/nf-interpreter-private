//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#ifndef MCUCONF_NF_H
#define MCUCONF_NF_H

// SDIO host for the WLAN chip (DMA2 stream 3 or 6)
#define STM32_SDIO_NF_DMA_STREAM   STM32_DMA_STREAM_ID(2, 3)
#define STM32_SDIO_NF_DMA_PRIORITY 3
#define STM32_SDIO_NF_IRQ_PRIORITY 9

#endif // MCUCONF_NF_H
