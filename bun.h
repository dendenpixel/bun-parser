#ifndef BUN_H
#define BUN_H

#include <stdint.h>
#include <stdio.h>

typedef enum {
    BUN_OK            = 0,
    BUN_MALFORMED     = 1,
    BUN_UNSUPPORTED   = 2,
    BUN_ERR_IO        = 3,   /* generic I/O error */
    BUN_ERR_NOT_FOUND = 4    /* file does not exist (errno == ENOENT at open) */
    /* Additional codes in the range 3-10 may be added; document them
       in the report. */
} bun_result_t;

//
// Data types 
// All multi-byte integers are little-endian on disk.
//

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

//
// On-disk structures 
//

#define BUN_MAGIC         0x304E5542u   // "BUN0" in little-endian
#define BUN_VERSION_MAJOR 1
#define BUN_VERSION_MINOR 0

#define BUN_FLAG_ENCRYPTED  0x1u
#define BUN_FLAG_EXECUTABLE 0x2u

// Sanity cap on asset name length when the string-table module reads them.
#define BUN_MAX_NAME_LEN  (64u * 1024u)

// DoS-protection cap on the number of asset records the parser will load.
// 1,000,000 records * 48 bytes per record = ~48 MB of in-memory state.
// Files that exceed this are reported as BUN_UNSUPPORTED.
#define BUN_MAX_RECORDS   1000000u

// Spec section 4 - BUN file header.
typedef struct {
    u32 magic;
    u16 version_major;
    u16 version_minor;
    u32 asset_count;
    u64 asset_table_offset;
    u64 string_table_offset;
    u64 string_table_size;
    u64 data_section_offset;
    u64 data_section_size;
    u64 reserved;
} BunHeader;

// Spec section 5 - asset entry record.
typedef struct {
    u32 name_offset;
    u32 name_length;
    u64 data_offset;
    u64 data_size;
    u64 uncompressed_size;
    u32 compression;
    u32 type;
    u32 checksum;
    u32 flags;
} BunAssetRecord;

//
// Expected on-disk sizes, usable in assertions or static_asserts.
//

#define BUN_HEADER_SIZE       60
#define BUN_ASSET_RECORD_SIZE 48

//
// Parse context
//
// Holds everything that flows between the parse stages: the open file,
// validated records, and the running list of spec violations found so far.
// Parsing functions never print; main.c reads from here to produce output.
//

typedef struct {
    FILE   *file;            // open file handle
    u64    file_size;       // total file size in bytes

    // Asset records (populated by bun_parse_assets).
    BunAssetRecord *records;
    size_t          record_count;

    // Asset names (populated by the string-table module, not yet implemented).
    // Parallel to `records`; entries may be NULL if a name failed validation.
    char          **names;

    // Violation list. Each entry is a malloc'd string describing one
    // spec violation. Populated by parsing functions via add_violationf.
    char          **violations;
    size_t          n_violations;
    size_t          cap_violations;
} BunParseContext;

//
// Public API
//
// Parsing functions return result codes only and do not print to stdout
// or stderr. Callers (e.g. main.c) inspect the result code and the
// content of `ctx` (records, names, violations) to produce output.
//

/**
 * Open a BUN file and populate ctx. Returns BUN_ERR_IO if the file
 * cannot be opened or its size determined.
 */
bun_result_t bun_open(const char *path, BunParseContext *ctx);

/**
 * Parse and validate the BUN header from ctx->file, populating *header.
 * Appends violation messages to ctx on failure.
 * Returns BUN_OK, BUN_MALFORMED, or BUN_UNSUPPORTED.
 */
bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header);

/**
 * Parse and validate all asset records. Called after bun_parse_header().
 * On BUN_OK, ctx->records holds ctx->record_count validated records.
 * Appends violation messages to ctx on failure.
 * Returns BUN_OK, BUN_MALFORMED, BUN_UNSUPPORTED, or BUN_ERR_IO.
 */
bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header);

/**
 * Close the file handle and free all allocations owned by ctx.
 * Safe to call on a context whose file is already NULL (no-op).
 */
bun_result_t bun_close(BunParseContext *ctx);

#endif // BUN_H

