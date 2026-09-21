//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#ifndef TARGET_BLOCKSTORAGE_H
#define TARGET_BLOCKSTORAGE_H

// this device has 1 block storage devices
#define TARGET_BLOCKSTORAGE_COUNT 1

// SOC flash controller (devicetree node)
#define FLASH_DRIVER_NODE DT_CHOSEN(zephyr_flash_controller)

#endif // TARGET_BLOCKSTORAGE_H