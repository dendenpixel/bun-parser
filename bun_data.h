#ifndef BUN_DATA_H
#define BUN_DATA_H

#include "bun.h"

typedef struct {
    u8     preview[60];
    size_t preview_len;
    int    is_printable;
    char   error[256];
} BunAssetDataResult;

/**
 * Read, validate, and display the data for a single asset.
 * Handles uncompressed and RLE-compressed data.
 * Returns BUN_OK, BUN_MALFORMED, or BUN_UNSUPPORTED.
 */
bun_result_t bun_process_asset_data(BunParseContext *ctx, const BunHeader *header, const BunAssetRecord *record);

#endif // BUN_DATA_H