//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// WHD resource source: WLAN firmware and NVRAM provided by the target (g_wifiWhdPlatform).
// The CLM blob is reported with size 0 (chips like the 43362 have it built into the firmware).

#include <string.h>
#include <whd.h>
#include <whd_resource_api.h>
#include "nf_wifi_whd.h"

// block size used to hand the resources to WHD
#define WHD_RESOURCE_BLOCK_SIZE 1024U

static const nf_wifi_whd_resource_t *GetResource(whd_resource_type_t resource)
{
    switch (resource)
    {
        case WHD_RESOURCE_WLAN_FIRMWARE:
            return &g_wifiWhdPlatform.firmware;

        case WHD_RESOURCE_WLAN_NVRAM:
            return &g_wifiWhdPlatform.nvram;

        default:
            return NULL;
    }
}

static uint32_t ResourceSize(whd_driver_t whd_drv, whd_resource_type_t resource, uint32_t *size_out)
{
    const nf_wifi_whd_resource_t *res = GetResource(resource);

    (void)whd_drv;

    // CLM (or any other resource) not available: size 0
    *size_out = (res != NULL && res->data != NULL && res->size != NULL) ? *res->size : 0;

    return WHD_SUCCESS;
}

static uint32_t GetResourceBlockSize(whd_driver_t whd_drv, whd_resource_type_t type, uint32_t *size_out)
{
    (void)whd_drv;
    (void)type;

    *size_out = WHD_RESOURCE_BLOCK_SIZE;

    return WHD_SUCCESS;
}

static uint32_t GetResourceNoOfBlocks(whd_driver_t whd_drv, whd_resource_type_t type, uint32_t *block_count)
{
    uint32_t size;

    ResourceSize(whd_drv, type, &size);

    *block_count = (size + WHD_RESOURCE_BLOCK_SIZE - 1) / WHD_RESOURCE_BLOCK_SIZE;

    return WHD_SUCCESS;
}

static uint32_t GetResourceBlock(
    whd_driver_t whd_drv,
    whd_resource_type_t type,
    uint32_t blockno,
    const uint8_t **data,
    uint32_t *size_out)
{
    const nf_wifi_whd_resource_t *res = GetResource(type);
    uint32_t offset = blockno * WHD_RESOURCE_BLOCK_SIZE;

    (void)whd_drv;

    if (res == NULL || res->data == NULL || res->size == NULL || offset >= *res->size)
    {
        *size_out = 0;
        return WHD_BADARG;
    }

    *data = res->data + offset;
    *size_out = *res->size - offset;

    if (*size_out > WHD_RESOURCE_BLOCK_SIZE)
    {
        *size_out = WHD_RESOURCE_BLOCK_SIZE;
    }

    return WHD_SUCCESS;
}

static uint32_t ResourceRead(
    whd_driver_t whd_drv,
    whd_resource_type_t type,
    uint32_t offset,
    uint32_t size,
    uint32_t *size_out,
    void *buffer)
{
    const nf_wifi_whd_resource_t *res = GetResource(type);

    (void)whd_drv;

    if (res == NULL || res->data == NULL || res->size == NULL || offset >= *res->size)
    {
        *size_out = 0;
        return WHD_BADARG;
    }

    if (size > *res->size - offset)
    {
        size = *res->size - offset;
    }

    memcpy(buffer, res->data + offset, size);
    *size_out = size;

    return WHD_SUCCESS;
}

whd_resource_source_t g_whdResourceOps = {
    .whd_resource_size = ResourceSize,
    .whd_get_resource_block = GetResourceBlock,
    .whd_get_resource_no_of_blocks = GetResourceNoOfBlocks,
    .whd_get_resource_block_size = GetResourceBlockSize,
    .whd_resource_read = ResourceRead,
};
