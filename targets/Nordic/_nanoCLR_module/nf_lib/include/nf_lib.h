//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // nanoFramework entry point: wire_uart is the UART device used by the Wire Protocol (debugger)
    int start_nf(const struct device *wire_uart);

#ifdef __cplusplus
}
#endif
