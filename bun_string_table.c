#define _FILE_OFFSET_BITS 64

#include "bun_string_table.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ================================================================
// Internal helpers
// ================================================================

// Printable ASCII range per BUN spec section 6: 0x20 (space) .. 0x7E (~).
static int is_printable_ascii(u8 b) {
    return b >= 0x20u && b <= 0x7Eu;
}

// Append a violation message to ctx->violations.
// Mirrors the same helper in bun_parse.c so this module can report
// violations without depending on bun_parse.c's internal statics.
static void add_violation(BunParseContext *ctx, const char *msg) {
    if (ctx->n_violations == ctx->cap_violations) {
        size_t ncap = ctx->cap_violations ? ctx->cap_violations * 2 : 8;
        char **nv = realloc(ctx->violations, ncap * sizeof(char *));
        if (!nv) return;   // best-effort on OOM; never crash
        ctx->violations = nv;
        ctx->cap_violations = ncap;
    }
    char *copy = strdup(msg);
    if (!copy) return;
    ctx->violations[ctx->n_violations++] = copy;
}

static void add_violationf(BunParseContext *ctx, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    add_violation(ctx, buf);
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

// ================================================================
// Public API
// ================================================================

bun_result_t bun_validate_asset_name(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *record)
{
    // Rule 1: names must be non-empty.
    if (record->name_length == 0) {
        return BUN_MALFORMED;
    }

    // Rule 2: the name must lie inside the string table (no overflow).
    u64 file_offset;
    u64 name_end_in_table;
    if (!compute_name_file_offset(header, record,
                                  &file_offset, &name_end_in_table)) {
        return BUN_MALFORMED;
    }
    if (name_end_in_table > header->string_table_size) {
        return BUN_MALFORMED;
    }

    // Defensive: absolute end must also lie within the file.
    // (The header validator should guarantee string_table_offset +
    // string_table_size <= file_size, but we check here so this function
    // is safe to call independently.)
    if (ctx->file_size < 0) {
        return BUN_ERR_IO;
    }
    u64 file_end = file_offset + (u64)record->name_length;
    if (file_end > (u64)ctx->file_size) {
        return BUN_MALFORMED;
    }

    // Rule 3: stream-validate every byte as printable ASCII.
    if (fseeko(ctx->file, (off_t)file_offset, SEEK_SET) != 0) {
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
        add_violation(ctx, "string table: records array is NULL");
        return BUN_MALFORMED;
    }

    bun_result_t worst = BUN_OK;

    for (u32 i = 0; i < asset_count; i++) {
        const BunAssetRecord *r = &records[i];
        bun_result_t result = bun_validate_asset_name(ctx, header, r);

        if (result == BUN_OK) continue;

        // Report a specific violation message for this asset.
        if (r->name_length == 0) {
            add_violationf(ctx, "asset[%u]: name_length is zero "
                           "(spec section 6: names must be non-zero length)", i);
        } else {
            u64 end = (u64)r->name_offset + (u64)r->name_length;
            if (end > header->string_table_size) {
                add_violationf(ctx,
                    "asset[%u]: name range [%u+%u=%llu] exceeds "
                    "string_table_size %llu (spec section 6)",
                    i, r->name_offset, r->name_length,
                    (unsigned long long)end,
                    (unsigned long long)header->string_table_size);
            } else {
                add_violationf(ctx,
                    "asset[%u]: name contains non-printable character "
                    "(spec section 6: names must be printable ASCII 0x20-0x7E)", i);
            }
        }

        if (result == BUN_ERR_IO) return BUN_ERR_IO;  // I/O failure: stop now
        worst = BUN_MALFORMED;
    }

    return worst;
}

bun_result_t bun_populate_names(
    BunParseContext *ctx,
    const BunHeader *header)
{
    if (ctx->record_count == 0) return BUN_OK;
    if (!ctx->records) return BUN_ERR_IO;

    // Allocate the names array. bun_close() is responsible for freeing it.
    ctx->names = calloc(ctx->record_count, sizeof(char *));
    if (!ctx->names) return BUN_ERR_IO;

    // Brief section 5.2b: display the first ~60 characters of each name.
    char buf[61];
    bun_result_t worst = BUN_OK;

    for (size_t i = 0; i < ctx->record_count; i++) {
        bun_result_t r = bun_read_asset_name_prefix(
            ctx, header, &ctx->records[i], buf, sizeof(buf));
        if (r != BUN_OK) {
            ctx->names[i] = NULL;
            if (r == BUN_ERR_IO) return BUN_ERR_IO;
            worst = BUN_MALFORMED;
            continue;
        }
        ctx->names[i] = strdup(buf);
        if (!ctx->names[i]) return BUN_ERR_IO;   // OOM
    }

    return worst;
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

    if (fseeko(ctx->file, (off_t)file_offset, SEEK_SET) != 0) {
        return BUN_ERR_IO;
    }
    if (fread(out, 1, to_read, ctx->file) != to_read) {
        out[0] = '\0';
        return BUN_ERR_IO;
    }
    out[to_read] = '\0';
    return BUN_OK;
}
