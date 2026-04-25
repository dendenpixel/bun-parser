#include "bun_string_table.h"

#include <stdio.h>
#include <stdint.h>

// Printable ASCII range per BUN spec section 6: 0x20 (space) .. 0x7E (~).
static int is_printable_ascii(u8 b) {
    return b >= 0x20u && b <= 0x7Eu;
}

// Compute the absolute file offset of an asset name and verify that
// (name_offset + name_length) does not overflow.
//
// Returns 1 on success and writes:
//   *out_file_offset       = header->string_table_offset + record->name_offset
//   *out_name_end_in_table = record->name_offset + record->name_length
// Returns 0 if either addition would overflow its type.
static int compute_name_file_offset(
    const BunHeader      *header,
    const BunAssetRecord *record,
    u64                  *out_file_offset,
    u64                  *out_name_end_in_table)
{
    // name_offset + name_length is declared u32 on disk; guard against
    // wraparound when computing the end of the name within the table.
    if ((u64)record->name_offset + (u64)record->name_length > (u64)UINT32_MAX) {
        return 0;
    }
    u64 name_end_in_table = (u64)record->name_offset + (u64)record->name_length;

    // string_table_offset is u64; guard the absolute-offset computation too.
    if ((u64)record->name_offset > UINT64_MAX - header->string_table_offset) {
        return 0;
    }
    u64 file_offset = header->string_table_offset + (u64)record->name_offset;

    *out_file_offset       = file_offset;
    *out_name_end_in_table = name_end_in_table;
    return 1;
}

bun_result_t bun_validate_asset_name(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *record)
{
    // Rule 1: names must be non-empty.
    if (record->name_length == 0) {
        return BUN_MALFORMED;
    }

    // Rule 2: the name must lie inside the string table.
    u64 file_offset;
    u64 name_end_in_table;
    if (!compute_name_file_offset(header, record,
                                  &file_offset, &name_end_in_table)) {
        return BUN_MALFORMED;
    }
    if (name_end_in_table > header->string_table_size) {
        return BUN_MALFORMED;
    }

    // Independent check: the string table itself must lie in the file.
    // The header validator should already guarantee this, but this keeps
    // the function safe to call without assuming the header was checked.
    if (ctx->file_size < 0) {
        return BUN_ERR_IO;
    }
    u64 file_end = file_offset + (u64)record->name_length;
    if (file_end > (u64)ctx->file_size) {
        return BUN_MALFORMED;
    }

    // Rule 3: stream-validate every byte as printable ASCII.
    if (fseek(ctx->file, (long)file_offset, SEEK_SET) != 0) {
        return BUN_ERR_IO;
    }
    u8 buf[256];
    u32 remaining = record->name_length;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(buf)
                     ? (size_t)remaining
                     : sizeof(buf);
        if (fread(buf, 1, chunk, ctx->file) != chunk) {
            return BUN_ERR_IO;
        }
        for (size_t i = 0; i < chunk; i++) {
            if (!is_printable_ascii(buf[i])) {
                return BUN_MALFORMED;
            }
        }
        remaining -= (u32)chunk;
    }

    return BUN_OK;
}

bun_result_t bun_validate_string_table(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *records,
    u32                   asset_count)
{
    if (records == NULL && asset_count > 0) {
        return BUN_MALFORMED;
    }
    for (u32 i = 0; i < asset_count; i++) {
        bun_result_t r = bun_validate_asset_name(ctx, header, &records[i]);
        if (r != BUN_OK) {
            return r;
        }
    }
    return BUN_OK;
}

bun_result_t bun_read_asset_name_prefix(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *record,
    char                 *out,
    size_t                out_size)
{
    if (out == NULL || out_size == 0) {
        return BUN_ERR_IO;
    }
    out[0] = '\0';

    u64 file_offset;
    u64 name_end_in_table;
    if (!compute_name_file_offset(header, record,
                                  &file_offset, &name_end_in_table)) {
        return BUN_ERR_IO;
    }

    size_t to_read = record->name_length;
    if (to_read > out_size - 1) {
        to_read = out_size - 1;
    }

    if (fseek(ctx->file, (long)file_offset, SEEK_SET) != 0) {
        return BUN_ERR_IO;
    }
    if (fread(out, 1, to_read, ctx->file) != to_read) {
        out[0] = '\0';
        return BUN_ERR_IO;
    }
    out[to_read] = '\0';
    return BUN_OK;
}
