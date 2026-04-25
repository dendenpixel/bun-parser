#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bun.h"
#include "bun_data.h"

static void display_preview(const u8 *buf, size_t len) {
    int is_printable = 1;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x20 || buf[i] > 0x7E) {
            is_printable = 0;
            break;
        }
    }
    if (is_printable) {
        printf("  data (text): %.*s\n", (int)len, buf);
    } else {
        printf("  data (hex): ");
        for (size_t i = 0; i < len; i++) {
            printf("%02x ", buf[i]);
        }
        printf("\n");
    }
}

bun_result_t bun_process_asset_data(BunParseContext *ctx, const BunHeader *header, const BunAssetRecord *record) {

    // validate data lies within data section bounds
    if (record->data_offset + record->data_size > header->data_section_size) {
        fprintf(stderr, "asset data out of bounds: offset %lu + size %lu exceeds data section size %lu\n",
                record->data_offset, record->data_size, header->data_section_size);
        return BUN_MALFORMED;
    }

    // validate checksum field
    if (record->checksum != 0) {
        fprintf(stderr, "unsupported feature: non-zero CRC-32 checksum\n");
        return BUN_UNSUPPORTED;
    }

    // validate compression field
    if (record->compression == 2) {
        fprintf(stderr, "unsupported compression type: zlib (2)\n");
        return BUN_UNSUPPORTED;
    }
    if (record->compression > 2) {
        fprintf(stderr, "unsupported compression type: %u\n", record->compression);
        return BUN_UNSUPPORTED;
    }
    if (record->compression == 0 && record->uncompressed_size != 0) {
        fprintf(stderr, "uncompressed asset has non-zero uncompressed_size: %lu\n", record->uncompressed_size);
        return BUN_MALFORMED;
    }
    if (record->compression != 0 && record->uncompressed_size == 0) {
        fprintf(stderr, "compressed asset has zero uncompressed_size\n");
        return BUN_MALFORMED;
    }

    // seek to the asset data in the file
    u64 absolute_offset = header->data_section_offset + record->data_offset;
    if (fseeko(ctx->file, (off_t)absolute_offset, SEEK_SET) != 0) {
        fprintf(stderr, "failed to seek to asset data at offset %lu\n", absolute_offset);
        return BUN_ERR_IO;
    }

    // uncompressed case
    if (record->compression == 0) {
        u8 buf[4096];
        u64 remaining = record->data_size;
        int displayed = 0;

        while (remaining > 0) {
            size_t to_read = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
            size_t n = fread(buf, 1, to_read, ctx->file);
            if (n != to_read) {
                fprintf(stderr, "unexpected end of file reading asset data\n");
                return BUN_ERR_IO;
            }

            if (!displayed) {
                size_t display_len = n < 60 ? n : 60;
                display_preview(buf, display_len);
                displayed = 1;
            }

            remaining -= n;
        }

    // RLE case
    } else if (record->compression == 1) {
        if (record->data_size % 2 != 0) {
            fprintf(stderr, "RLE data size is odd (%lu bytes), must be even\n", record->data_size);
            return BUN_MALFORMED;
        }

        u8 buf[4096];
        u64 remaining = record->data_size;
        u64 decompressed_total = 0;
        u8 display_buf[60];
        size_t display_pos = 0;

        while (remaining > 0) {
            size_t to_read = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
            if (to_read % 2 != 0) to_read--;

            size_t n = fread(buf, 1, to_read, ctx->file);
            if (n != to_read) {
                fprintf(stderr, "unexpected end of file reading RLE data\n");
                return BUN_ERR_IO;
            }

            for (size_t i = 0; i < n; i += 2) {
                u8 count = buf[i];
                u8 value = buf[i + 1];

                if (count == 0) {
                    fprintf(stderr, "RLE pair has zero count at file offset %lu\n",
                            absolute_offset + (record->data_size - remaining) + i);
                    return BUN_MALFORMED;
                }

                decompressed_total += count;

                // collect first 60 decompressed bytes for preview
                for (u8 j = 0; j < count && display_pos < 60; j++) {
                    display_buf[display_pos++] = value;
                }
            }

            remaining -= n;
        }

        // verify decompressed size matches record
        if (decompressed_total != record->uncompressed_size) {
            fprintf(stderr, "RLE decompressed size %lu does not match expected %lu\n",
                    decompressed_total, record->uncompressed_size);
            return BUN_MALFORMED;
        }

        display_preview(display_buf, display_pos);
    }

    return BUN_OK;
}