//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// WHD buffer interface on top of lwIP pbufs.
// WHD needs contiguous buffers, so they are allocated from the lwIP heap (PBUF_RAM).

#include <ch.h>
#include <whd.h>
#include <whd_network_types.h>
#include <lwip/pbuf.h>

static whd_result_t BufferGet(whd_buffer_t *buffer, whd_buffer_dir_t direction, uint16_t size, uint32_t timeout_ms)
{
    struct pbuf *p;
    uint32_t elapsed = 0;

    (void)direction;

    if (buffer == NULL || size == 0)
    {
        return WHD_BADARG;
    }

    while (true)
    {
        p = pbuf_alloc(PBUF_RAW, size, PBUF_RAM);

        if (p != NULL)
        {
            *buffer = p;
            return WHD_SUCCESS;
        }

        if (elapsed >= timeout_ms)
        {
            break;
        }

        chThdSleepMilliseconds(1);
        elapsed++;
    }

    *buffer = NULL;

    return WHD_BUFFER_UNAVAILABLE_TEMPORARY;
}

static void BufferRelease(whd_buffer_t buffer, whd_buffer_dir_t direction)
{
    (void)direction;

    if (buffer != NULL)
    {
        pbuf_free((struct pbuf *)buffer);
    }
}

static uint8_t *BufferGetCurrentPieceDataPointer(whd_buffer_t buffer)
{
    return (uint8_t *)((struct pbuf *)buffer)->payload;
}

static uint16_t BufferGetCurrentPieceSize(whd_buffer_t buffer)
{
    return ((struct pbuf *)buffer)->len;
}

// WHD only uses this to shrink a buffer
static whd_result_t BufferSetSize(whd_buffer_t buffer, uint16_t size)
{
    struct pbuf *p = (struct pbuf *)buffer;

    if (size > p->len)
    {
        return WHD_BUFFER_SIZE_SET_ERROR;
    }

    p->len = size;
    p->tot_len = size;

    return WHD_SUCCESS;
}

// positive amount: remove from the front (skip a header), negative amount: add to the front (prepend a header)
static whd_result_t BufferAddRemoveAtFront(whd_buffer_t *buffer, int32_t add_remove_amount)
{
    struct pbuf *p = (struct pbuf *)*buffer;
    u8_t result;

    if (add_remove_amount > 0)
    {
        result = pbuf_remove_header(p, (size_t)add_remove_amount);
    }
    else
    {
        result = pbuf_add_header(p, (size_t)(-add_remove_amount));
    }

    return (result == 0) ? WHD_SUCCESS : WHD_BUFFER_POINTER_MOVE_ERROR;
}

whd_buffer_funcs_t g_whdBufferOps = {
    .whd_host_buffer_get = BufferGet,
    .whd_buffer_release = BufferRelease,
    .whd_buffer_get_current_piece_data_pointer = BufferGetCurrentPieceDataPointer,
    .whd_buffer_get_current_piece_size = BufferGetCurrentPieceSize,
    .whd_buffer_set_size = BufferSetSize,
    .whd_buffer_add_remove_at_front = BufferAddRemoveAtFront,
};
