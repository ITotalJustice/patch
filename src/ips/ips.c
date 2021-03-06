/**
 * Copyright 2022 TotalJustice.
 * SPDX-License-Identifier: Zlib
 */

/* SOURCE: https://zerosoft.zophar.net/ips.php */
#include "ips.h"
#include <string.h>

/* header is 5 bytes plus at least 2 bytes for input and output size */
#define PATCH_HEADER_SIZE 0x5
#define PATCH_MIN_SIZE 0x9
#define PATCH_MAX_SIZE 0x1000000 /* 16MiB */

#define EOF_MAGIC 0x454F46
#define RLE_ENCODING 0


static uint8_t safe_read(const uint8_t* data, size_t* offset, const size_t size)
{
    if (*offset < size)
    {
        const uint8_t value = data[*offset];
        ++*offset;

        return value;
    }

    return 0;
}

/* basically a memcpy */
static uint16_t safe_read2(const uint8_t* data, size_t* offset, const size_t size)
{
    *offset += 2;

    if (*offset > size)
    {
        return 0;
    }

    return (data[*offset - 2] << 8) | (data[*offset - 1]);
}

static uint32_t safe_read3(const uint8_t* data, size_t* offset, const size_t size)
{
    *offset += 3;

    if (*offset > size)
    {
        return 0;
    }

    return (data[*offset - 3] << 16) | (data[*offset - 2] << 8) | (data[*offset - 1]);
}

static void safe_write(uint8_t* data, const uint8_t value, size_t* offset, const size_t size)
{
    if (*offset < size)
    {
        data[*offset] = value;
        ++*offset;
    }
}

bool ips_verify_header(const uint8_t* patch, const size_t patch_size)
{
    if (!patch || patch_size < PATCH_HEADER_SIZE)
    {
        return false;
    }

    if (patch[0] != 'P' || patch[1] != 'A' ||  patch[2] != 'T' || patch[3] != 'C' || patch[4] != 'H')
    {
        return false;
    }

    return true;
}

/*
    sadly, there's no clean way to get the dst_size.
    the only way is to parse the entire patch, and then return
    the final output size.

    really, this isn't so bad if i cache the data when scanning the entire
    patch, that way, the second pass is a lot faster at least.
    i currently don't do this.
*/
bool ips_get_size(const uint8_t* patch, size_t patch_size, size_t* dst_size)
{
    size_t patch_offset = PATCH_HEADER_SIZE;
    size_t output_size = 0;

    while (patch_offset < patch_size)
    {
        size_t new_output_size = 0;
        const size_t offset = safe_read3(patch, &patch_offset, patch_size);

        /* check if last 3 bytes were EOF */
        if (offset == EOF_MAGIC)
        {
            break;
        }

        const uint16_t size = safe_read2(patch, &patch_offset, patch_size);

        if (size == RLE_ENCODING)
        {
            const uint16_t rle_size = safe_read2(patch, &patch_offset, patch_size);
            patch_offset++; // safe_read() for the value

            new_output_size = offset + rle_size;
        }
        else
        {
            patch_offset += size;
            new_output_size = offset + size;
        }

        if (new_output_size > output_size)
        {
            output_size = new_output_size;
        }
    }

    *dst_size = output_size;
    return true;
}

/* applies the ups patch to the dst data */
bool ips_patch(
    uint8_t* dst, const size_t dst_size,
    const uint8_t* src, const size_t src_size,
    const uint8_t* patch, const size_t patch_size
) {
    if (!dst || !dst_size || !src || !src_size || !patch)
    {
        return false;
    }

    if (patch_size < PATCH_MIN_SIZE || patch_size > PATCH_MAX_SIZE)
    {
        return false;
    }

    size_t patch_offset = 0;

    if (!ips_verify_header(patch, patch_size))
    {
        return false;
    }

    #define ASSERT_BOUNDS(offset, size) if (offset >= size) { return false; }

    patch_offset = PATCH_HEADER_SIZE;

    if (src_size < dst_size)
    {
        memcpy(dst, src, src_size);
    }

    while (patch_offset < patch_size)
    {
        size_t offset = safe_read3(patch, &patch_offset, patch_size);

        /* check if last 3 bytes were EOF */
        if (offset == EOF_MAGIC)
        {
            break;
        }

        uint16_t size = safe_read2(patch, &patch_offset, patch_size);

        if (size == RLE_ENCODING)
        {
            uint16_t rle_size = safe_read2(patch, &patch_offset, patch_size);
            const uint8_t value = safe_read(patch, &patch_offset, patch_size);

            while (rle_size--)
            {
                ASSERT_BOUNDS(offset, dst_size);
                ASSERT_BOUNDS(patch_offset, patch_size);

                safe_write(dst, value, &offset, dst_size);
            }
        }
        else
        {
            while (size--)
            {
                ASSERT_BOUNDS(offset, dst_size);
                ASSERT_BOUNDS(patch_offset, patch_size);

                const uint8_t value = safe_read(patch, &patch_offset, patch_size);
                safe_write(dst, value, &offset, dst_size);
            }
        }
    }

    return true;
}
