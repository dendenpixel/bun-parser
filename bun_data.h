#ifndef BUN_DATA_H
#define BUN_DATA_H

#include "bun.h"

typedef struct {
    u8     preview[60];   // first 60 bytes of decompressed data
    size_t preview_len;   // how many bytes are in preview
    int    is_printable;  // 1 if preview is printable ASCII, 0 if binary
    char   error[256];    // human-readable error message if result != BUN_OK
} BunAssetDataResult;

/**
 * Read, validate, and extract a preview of the data for a single asset.
 * Handles uncompressed and RLE-compressed data.
 * On success, populates out->preview, out->preview_len, out->is_printable.
 * On failure, populates out->error with a human-readable message.
 * Returns BUN_OK, BUN_MALFORMED, BUN_UNSUPPORTED, or BUN_ERR_IO.
 */
bun_result_t bun_process_asset_data(BunParseContext *ctx, const BunHeader *header, const BunAssetRecord *record, BunAssetDataResult *out);

#endif // BUN_DATA_H