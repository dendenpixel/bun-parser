#ifndef BUN_STRING_TABLE_H
#define BUN_STRING_TABLE_H

#include "bun.h"

#include <stddef.h>

//
// String-table handling (per BUN spec section 6).
//
// The string table is a block of bytes holding asset names. Names are not
// null-terminated; each asset record identifies its name with
// (name_offset, name_length), where name_offset is relative to the start
// of the string table.
//
// Validation rules applied here:
//   1. name_length must be non-zero.
//   2. [name_offset, name_offset + name_length) must lie entirely within
//      [0, string_table_size), with no integer overflow.
//   3. Every byte of the name must be printable ASCII (0x20..0x7E).
//
// The functions below do not read the whole string table into memory;
// they seek to each name and stream-validate it in small fixed-size
// chunks. This keeps memory use bounded even on very large files.
//

/**
 * Validate a single asset name against the string table.
 *
 * Reads the name from ctx->file using the offsets supplied in `header`
 * and `record`. The file position of ctx->file is modified.
 *
 * Returns:
 *   BUN_OK        - the name is valid.
 *   BUN_MALFORMED - any of the rules above was violated.
 *   BUN_ERR_IO    - a seek or read on ctx->file failed.
 */
bun_result_t bun_validate_asset_name(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *record);

/**
 * Validate every asset name in `records[0..asset_count)`.
 *
 * Checks all names even after a failure, appending one violation message
 * per bad name to ctx->violations. Returns BUN_OK only if every name
 * is valid, BUN_MALFORMED if any fail, BUN_ERR_IO on I/O error.
 */
bun_result_t bun_validate_string_table(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *records,
    u32                   asset_count);

/**
 * Copy a null-terminated prefix of an asset name into `out` for display.
 *
 * At most `out_size - 1` bytes of the name are read; `out` is always
 * null-terminated on success. Useful for the "first ~60 characters"
 * display required by the project brief.
 *
 * Precondition: out_size > 0.
 *
 * Returns BUN_OK on success, BUN_ERR_IO on I/O or argument error.
 */
bun_result_t bun_read_asset_name_prefix(
    BunParseContext      *ctx,
    const BunHeader      *header,
    const BunAssetRecord *record,
    char                 *out,
    size_t                out_size);

/**
 * Populate ctx->names with a display-ready prefix of each asset name.
 *
 * Must be called after bun_parse_assets() has populated ctx->records.
 * Allocates ctx->names as an array of ctx->record_count heap strings,
 * each holding up to 60 characters of the corresponding asset name
 * (null-terminated). Entries are NULL for names that could not be read.
 *
 * bun_close() will free ctx->names and each entry.
 *
 * Returns BUN_OK if all names were read, BUN_MALFORMED if any failed,
 * BUN_ERR_IO on allocation failure or I/O error.
 */
bun_result_t bun_populate_names(
    BunParseContext *ctx,
    const BunHeader *header);

#endif // BUN_STRING_TABLE_H
