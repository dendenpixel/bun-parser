#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bun.h"
#include "bun_data.h"

bun_result_t bun_process_asset_data(BunParseContext *ctx, const BunHeader *header, const BunAssetRecord *record, BunAssetDataResult *out) {

    // zero out the result struct
    memset(out, 0, sizeof(*out));

    // validate data lies within data section bounds
    // written as direct addition per spec wording: data_offset + data_size <= data_section_size
    if (record->data_offset + record->data_size > header->data_section_size) {
        snprintf(out->error, sizeof(out->error),
                "asset data out of bounds: offset %lu + size %lu exceeds data section size %lu",
                record->data_offset, record->data_size, header->data_section_size);
        return BUN_MALFORMED;
    }

    // validate flags field - only bits 0x1 and 0x2 are permitted
    if (record->flags & ~(BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE)) {
        snprintf(out->error, sizeof(out->error),
                "unsupported flags value: 0x%x", record->flags);
        return BUN_UNSUPPORTED;
    }

    // validate checksum field
    if (record->checksum != 0) {
        snprintf(out->error, sizeof(out->error),
                "unsupported feature: non-zero CRC-32 checksum");
        return BUN_UNSUPPORTED;
    }

    // validate compression field
    if (record->compression == 2) {
        snprintf(out->error, sizeof(out->error),
                "unsupported compression type: zlib (2)");
        return BUN_UNSUPPORTED;
    }
    if (record->compression > 2) {
        snprintf(out->error, sizeof(out->error),
                "unsupported compression type: %u", record->compression);
        return BUN_UNSUPPORTED;
    }
    if (record->compression == 0 && record->uncompressed_size != 0) {
        snprintf(out->error, sizeof(out->error),
                "uncompressed asset has non-zero uncompressed_size: %lu",
                record->uncompressed_size);
        return BUN_MALFORMED;
    }
    if (record->compression != 0 && record->uncompressed_size == 0) {
        snprintf(out->error, sizeof(out->error),
                "compressed asset has zero uncompressed_size");
        return BUN_MALFORMED;
    }

    // seek to the asset data in the file
    u64 absolute_offset = header->data_section_offset + record->data_offset;
    if (fseeko(ctx->file, (off_t)absolute_offset, SEEK_SET) != 0) {
        snprintf(out->error, sizeof(out->error),
                "failed to seek to asset data at offset %lu", absolute_offset);
        return BUN_ERR_IO;
    }

    // uncompressed case
    if (record->compression == 0) {
        u8 buf[4096];
        u64 remaining = record->data_size;
        int previewed = 0;

        while (remaining > 0) {
            size_t to_read = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
            size_t n = fread(buf, 1, to_read, ctx->file);
            if (n != to_read) {
                snprintf(out->error, sizeof(out->error),
                        "unexpected end of file reading asset data");
                return BUN_ERR_IO;
            }

            if (!previewed) {
                out->preview_len = n < 60 ? n : 60;
                memcpy(out->preview, buf, out->preview_len);
                previewed = 1;
            }

            remaining -= n;
        }

    // RLE case
    } else if (record->compression == 1) {
        if (record->data_size % 2 != 0) {
            snprintf(out->error, sizeof(out->error),
                    "RLE data size is odd (%lu bytes), must be even", record->data_size);
            return BUN_MALFORMED;
        }

        u8 buf[4096];
        u64 remaining = record->data_size;
        u64 decompressed_total = 0;

        while (remaining > 0) {
            size_t to_read = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
            if (to_read % 2 != 0) to_read--;

            size_t n = fread(buf, 1, to_read, ctx->file);
            if (n != to_read) {
                snprintf(out->error, sizeof(out->error),
                        "unexpected end of file reading RLE data");
                return BUN_ERR_IO;
            }

            for (size_t i = 0; i < n; i += 2) {
                u8 count = buf[i];
                u8 value = buf[i + 1];

                if (count == 0) {
                    snprintf(out->error, sizeof(out->error),
                            "RLE pair has zero count at file offset %lu",
                            absolute_offset + (record->data_size - remaining) + i);
                    return BUN_MALFORMED;
                }

                decompressed_total += count;

                // early exit if decompressed size already exceeds what record claims
                if (decompressed_total > record->uncompressed_size) {
                    snprintf(out->error, sizeof(out->error),
                            "RLE decompressed size exceeds expected %lu (possible RLE bomb)",
                            record->uncompressed_size);
                    return BUN_MALFORMED;
                }

                // collect first 60 decompressed bytes for preview
                for (u8 j = 0; j < count && out->preview_len < 60; j++) {
                    out->preview[out->preview_len++] = value;
                }
            }

            remaining -= n;
        }

        // verify decompressed size matches record exactly
        if (decompressed_total != record->uncompressed_size) {
            snprintf(out->error, sizeof(out->error),
                    "RLE decompressed size %lu does not match expected %lu",
                    decompressed_total, record->uncompressed_size);
            return BUN_MALFORMED;
        }
    }

    // determine if preview is printable ASCII
    out->is_printable = 1;
    for (size_t i = 0; i < out->preview_len; i++) {
        if (out->preview[i] < 0x20 || out->preview[i] > 0x7E) {
            out->is_printable = 0;
            break;
        }
    }

    return BUN_OK;
}