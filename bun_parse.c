#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>

#include "bun.h"
#include "bun_string_table.h"

// ================================================================
// Shared helpers
// ================================================================

static u16 read_u16_le(const u8 *buf, size_t offset) {
    return (u16)(buf[offset] | (unsigned)buf[offset + 1] << 8);
}

static u32 read_u32_le(const u8 *buf, size_t offset) {
    return  (u32)buf[offset]
         | ((u32)buf[offset + 1] << 8)
         | ((u32)buf[offset + 2] << 16)
         | ((u32)buf[offset + 3] << 24);
}

static u64 read_u64_le(const u8 *buf, size_t offset) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (u64)buf[offset + i] << (i * 8);
    }
    return v;
}

// 9.3 Requirements: no overlap
// We have already bounded it against the file size (fsize in this case), sum can't wrap.
static int sections_overlap(u64 offset1, u64 size1, u64 offset2, u64 size2) {
    if (size1 == 0 || size2 == 0) 
        return 0;  // empty ranges don't overlap
    return !(offset1 + size1 <= offset2 || offset2 + size2 <= offset1);
}

// Append a violation message to the context. Used directly for fixed strings.
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

// printf-style violation. Use this for everything that interpolates values.
static void add_violationf(BunParseContext *ctx, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    add_violation(ctx, buf);
}

// Escalate worst-result-so-far. MALFORMED beats UNSUPPORTED beats OK.
// IO errors are not ranked within, caller returns on BUN_ERR_IO early.
static void escalate(bun_result_t *status, bun_result_t new_status) {
    if (new_status == BUN_MALFORMED) {
        *status = BUN_MALFORMED;
    } else if (new_status == BUN_UNSUPPORTED && *status == BUN_OK) {
        *status = BUN_UNSUPPORTED;
    }
}

// fseeko wrapper that guards against u64 -> off_t overflow.
static int safe_seek(FILE *f, u64 pos) {
    if (pos > (u64)INT64_MAX) return -1;
    return fseeko(f, (off_t)pos, SEEK_SET);
}

// ================================================================
// File open / close
// ================================================================

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
    ctx->file = fopen(path, "rb");
    if (!ctx->file) {
        if (errno == ENOENT) return BUN_ERR_NOT_FOUND;
        return BUN_ERR_IO;
    }

    if (fseeko(ctx->file, 0, SEEK_END) != 0) {
        fclose(ctx->file);
        ctx->file = NULL;
        return BUN_ERR_IO;
    }

    off_t sz = ftello(ctx->file);
    if (sz < 0) {
        fclose(ctx->file);
        ctx->file = NULL;
        return BUN_ERR_IO;
    }
    ctx->file_size = (u64)sz;

    rewind(ctx->file);
    return BUN_OK;
}

bun_result_t bun_close(BunParseContext *ctx) {
    if (!ctx) return BUN_OK;

    // Free names (populated by the string-table module).
    if (ctx->names) {
        for (size_t i = 0; i < ctx->record_count; i++) {
            free(ctx->names[i]);
        }
        free(ctx->names);
        ctx->names = NULL;
    }

    // Free records.
    free(ctx->records);
    ctx->records = NULL;
    ctx->record_count = 0;

    // Free violations.
    for (size_t i = 0; i < ctx->n_violations; i++) {
        free(ctx->violations[i]);
    }
    free(ctx->violations);
    ctx->violations = NULL;
    ctx->n_violations = 0;
    ctx->cap_violations = 0;

    // Close file (allow no-op if already closed).
    if (!ctx->file) return BUN_OK;
    int res = fclose(ctx->file);
    ctx->file = NULL;
    return res ? BUN_ERR_IO : BUN_OK;
}

// ================================================================
// Header parsing 
// ================================================================

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
    u8 buf[BUN_HEADER_SIZE];

    if (ctx->file_size < BUN_HEADER_SIZE) {
        add_violationf(ctx,
            "file too short: %llu bytes, need at least %d for header",
            (unsigned long long)ctx->file_size, BUN_HEADER_SIZE);
        return BUN_MALFORMED;
    }

    if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
        add_violation(ctx, "failed to read header bytes from file");
        return BUN_ERR_IO;
    }

    // Decode every field. Cannot memcpy into BunHeader because in-memory
    // padding is not guaranteed to match on-disk layout (spec section 4 +
    // 4.1 notes).
    header->magic               = read_u32_le(buf,  0);
    header->version_major       = read_u16_le(buf,  4);
    header->version_minor       = read_u16_le(buf,  6);
    header->asset_count         = read_u32_le(buf,  8);
    header->asset_table_offset  = read_u64_le(buf, 12);
    header->string_table_offset = read_u64_le(buf, 20);
    header->string_table_size   = read_u64_le(buf, 28);
    header->data_section_offset = read_u64_le(buf, 36);
    header->data_section_size   = read_u64_le(buf, 44);
    header->reserved            = read_u64_le(buf, 52);  // 4.1.6 ignored

    // Collect all header violations rather than returning on the first.
    bun_result_t worst = BUN_OK;

    // magic must match exactly.
    if (header->magic != BUN_MAGIC) {
        add_violationf(ctx,
            "bad magic: got 0x%08X, expected 0x%08X",
            header->magic, BUN_MAGIC);
        escalate(&worst, BUN_MALFORMED);
    }

    //version must be exactly 1.0; otherwise UNSUPPORTED.
    if (header->version_major != BUN_VERSION_MAJOR ||
        header->version_minor != BUN_VERSION_MINOR) {
        add_violationf(ctx,
            "unsupported version: %u.%u (expected %d.%d)",
            header->version_major, header->version_minor,
            BUN_VERSION_MAJOR, BUN_VERSION_MINOR);
        escalate(&worst, BUN_UNSUPPORTED);
    }

    //all three offsets and two sizes must be divisible by 4.
    if (header->asset_table_offset % 4 != 0) {
        add_violationf(ctx,
            "asset_table_offset (%llu) not divisible by 4",
            (unsigned long long)header->asset_table_offset);
        escalate(&worst, BUN_MALFORMED);
    }
    if (header->string_table_offset % 4 != 0) {
        add_violationf(ctx,
            "string_table_offset (%llu) not divisible by 4",
            (unsigned long long)header->string_table_offset);
        escalate(&worst, BUN_MALFORMED);
    }
    if (header->string_table_size % 4 != 0) {
        add_violationf(ctx,
            "string_table_size (%llu) not divisible by 4",
            (unsigned long long)header->string_table_size);
        escalate(&worst, BUN_MALFORMED);
    }
    if (header->data_section_offset % 4 != 0) {
        add_violationf(ctx,
            "data_section_offset (%llu) not divisible by 4",
            (unsigned long long)header->data_section_offset);
        escalate(&worst, BUN_MALFORMED);
    }
    if (header->data_section_size % 4 != 0) {
        add_violationf(ctx,
            "data_section_size (%llu) not divisible by 4",
            (unsigned long long)header->data_section_size);
        escalate(&worst, BUN_MALFORMED);
    }

// Spec 9.2: all sections must fit within the file.
// Spec 9.3: no two sections may overlap.
    u64 fsize = ctx->file_size;
    u64 asset_table_size = (u64)header->asset_count * (u64)BUN_ASSET_RECORD_SIZE;
    if (asset_table_size > 0) {
        if (header->asset_table_offset > fsize || asset_table_size > fsize - header->asset_table_offset) {
            add_violationf(ctx,
                "asset table (offset %llu, size %llu) extends past end of file (%llu)",
                (unsigned long long)header->asset_table_offset,
                (unsigned long long)asset_table_size,
                (unsigned long long)fsize);
            escalate(&worst, BUN_MALFORMED);
        }
    }

    if (header->string_table_size > 0) {
        if (header->string_table_offset > fsize || header->string_table_size > fsize - header->string_table_offset) {
            add_violationf(ctx,
                "string table (offset %llu, size %llu) extends past end of file (%llu)",
                (unsigned long long)header->string_table_offset,
                (unsigned long long)header->string_table_size,
                (unsigned long long)fsize);
            escalate(&worst, BUN_MALFORMED);
        }
    }

    if (header->data_section_size > 0) {
        if (header->data_section_offset > fsize || header->data_section_size > fsize - header->data_section_offset) {
            add_violationf(ctx,
                "data section (offset %llu, size %llu) extends past end of file (%llu)",
                (unsigned long long)header->data_section_offset,
                (unsigned long long)header->data_section_size,
                (unsigned long long)fsize);
            escalate(&worst, BUN_MALFORMED);
        }
    }
    struct { const char *name; u64 off, thesize; } secs[] = {
        { "header",                                 0,           BUN_HEADER_SIZE },
        { "asset table",   header->asset_table_offset,          asset_table_size },
        { "string table", header->string_table_offset, header->string_table_size },
        { "data section", header->data_section_offset, header->data_section_size },
    };

    for (size_t i = 0; i < 4; i++) {
        for (size_t j = i + 1; j < 4; j++) {
            if (sections_overlap(secs[i].off, secs[i].thesize, secs[j].off, secs[j].thesize)) {
                add_violationf(ctx, "%s overlaps with %s", secs[i].name, secs[j].name);
                escalate(&worst, BUN_MALFORMED);
            }
        }
    }
    return worst;
}

// ================================================================
// Asset entry table 
// ================================================================

// Decode one 48-byte asset record from a byte buffer. Offsets per spec section 5.
static void decode_record(const u8 *buf, BunAssetRecord *r) {
    r->name_offset       = read_u32_le(buf,  0);
    r->name_length       = read_u32_le(buf,  4);
    r->data_offset       = read_u64_le(buf,  8);
    r->data_size         = read_u64_le(buf, 16);
    r->uncompressed_size = read_u64_le(buf, 24);
    r->compression       = read_u32_le(buf, 32);
    r->type              = read_u32_le(buf, 36);
    r->checksum          = read_u32_le(buf, 40);
    r->flags             = read_u32_le(buf, 44);
}

//name range fits inside string table. Pure arithmetic, no bytes read.
// removing check_name_bounds since string table does it.


// data range fits inside data section. Both fields are u64 but
// addition can still wrap, so guard explicitly.
static void check_data_bounds(BunParseContext *ctx, const BunHeader *h, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    if (r->data_size > 0 && r->data_offset > UINT64_MAX - r->data_size) {
        add_violationf(ctx,
            "asset[%u]: data_offset+data_size overflows u64 "
            "(offset=%llu, size=%llu)",
            i, (unsigned long long)r->data_offset,
            (unsigned long long)r->data_size);
        escalate(status, BUN_MALFORMED);
        return;
    }
    u64 end = r->data_offset + r->data_size;
    if (end > h->data_section_size) {
        add_violationf(ctx,
            "asset[%u]: data range [%llu+%llu=%llu] exceeds data_section_size %llu",
            i, (unsigned long long)r->data_offset,
            (unsigned long long)r->data_size,
            (unsigned long long)end,
            (unsigned long long)h->data_section_size);
        escalate(status, BUN_MALFORMED);
    }
}

// we accept compression 0 (none) and 1 (RLE); 2 (zlib) and others UNSUPPORTED.
static void check_compression(BunParseContext *ctx, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    if (r->compression == 0 || r->compression == 1) return;
    if (r->compression == 2) {
        add_violationf(ctx, "asset[%u]: zlib compression not supported", i);
    } else {
        add_violationf(ctx, "asset[%u]: unknown compression value %u",
                       i, r->compression);
    }
    escalate(status, BUN_UNSUPPORTED);
}

// if not compressed, uncompressed_size must be 0.
static void check_uncompressed_size(BunParseContext *ctx, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    if (r->compression == 0 && r->uncompressed_size != 0) {
        add_violationf(ctx,
            "asset[%u]: compression=none but uncompressed_size=%llu "
            "(must be 0 per spec 5.1 note 1)",
            i, (unsigned long long)r->uncompressed_size);
        escalate(status, BUN_MALFORMED);
    }
}

// RLE pairs are 2 bytes each, so on-disk size must be even.
static void check_rle_parity(BunParseContext *ctx, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    if (r->compression == 1 && (r->data_size % 2) != 0) {
        add_violationf(ctx,
            "asset[%u]: RLE data_size %llu is not even (spec 8.2)",
            i, (unsigned long long)r->data_size);
        escalate(status, BUN_MALFORMED);
    }
}

// we don't implement CRC-32 verification; non-zero checksum -> UNSUPPORTED.
static void check_checksum(BunParseContext *ctx, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    if (r->checksum != 0) {
        add_violationf(ctx,
            "asset[%u]: non-zero checksum 0x%08x "
            "(CRC-32 validation not supported)",
            i, r->checksum);
        escalate(status, BUN_UNSUPPORTED);
    }
}

// flags outside ENCRYPTED|EXECUTABLE -> UNSUPPORTED.
static void check_flags(BunParseContext *ctx, u32 i, const BunAssetRecord *r, bun_result_t *status) {
    u32 known = BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE;
    u32 unknown = r->flags & ~known;
    if (unknown != 0) {
        add_violationf(ctx,
            "asset[%u]: unknown flag bits set (flags=0x%08x, unknown=0x%08x)",
            i, r->flags, unknown);
        escalate(status, BUN_UNSUPPORTED);
    }
}

// Read one record's 48 bytes from disk with full bounds checking.
static bun_result_t read_record_bytes(BunParseContext *ctx, const BunHeader *header, u32 index, u8 buf[BUN_ASSET_RECORD_SIZE]) {
    u64 row = (u64)index * (u64)BUN_ASSET_RECORD_SIZE;
    if (header->asset_table_offset > UINT64_MAX - row) {
        add_violationf(ctx, "asset[%u]: record offset overflows u64", index);
        return BUN_MALFORMED;
    }
    u64 pos = header->asset_table_offset + row;

    // Bounds-check the record before reading. We have already checked the 
    // entire table against the file size, but check again at the record level
    // to get precise violation messages.

    if (pos > UINT64_MAX - BUN_ASSET_RECORD_SIZE || pos + BUN_ASSET_RECORD_SIZE > ctx->file_size) {
        add_violationf(ctx,
            "asset[%u]: record would read past end of file (pos=%llu, size=%llu)",
            index, (unsigned long long)pos, (unsigned long long)ctx->file_size);
        return BUN_MALFORMED;
    }

    if (safe_seek(ctx->file, pos) != 0) return BUN_ERR_IO;
    if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
        return BUN_ERR_IO;
    }
    return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
    if (!ctx || !header) return BUN_ERR_IO;

    // Bounds-check the entire asset table before any reads (spec 9.4 + 9.2).
    u64 table_bytes = (u64)header->asset_count * (u64)BUN_ASSET_RECORD_SIZE;
    if (header->asset_count != 0 &&
        table_bytes / header->asset_count != BUN_ASSET_RECORD_SIZE) {
        add_violation(ctx, "asset table size calculation overflows");
        return BUN_MALFORMED;
    }
    if (header->asset_table_offset > UINT64_MAX - table_bytes) {
        add_violation(ctx, "asset table end overflows u64");
        return BUN_MALFORMED;
    }
    if (header->asset_table_offset + table_bytes > ctx->file_size) {
        add_violation(ctx, "asset table extends past end of file");
        return BUN_MALFORMED;
    }

    if (header->asset_count == 0) return BUN_OK;

    #define BUN_MAX_RECORDS 1000000u
    if (header->asset_count > BUN_MAX_RECORDS) {
        add_violationf(ctx,
            "asset_count %u exceeds parser limit %u (DoS protection)",
            header->asset_count, BUN_MAX_RECORDS);
        return BUN_UNSUPPORTED;
    }

    #define BUN_MAX_RECORDS 1000000u   // 1M records = 56 MB in memory

    if (header->asset_count > BUN_MAX_RECORDS) {
        add_violationf(ctx,
            "asset_count %u exceeds parser limit %u",
            header->asset_count, BUN_MAX_RECORDS);
        return BUN_UNSUPPORTED;   // not malformed — file may be valid, we just won't load it
    }
    
    ctx->records = calloc(header->asset_count, sizeof(BunAssetRecord));
    if (!ctx->records) return BUN_ERR_IO;
    ctx->record_count = header->asset_count;

    bun_result_t worst = BUN_OK;

    for (u32 i = 0; i < header->asset_count; i++) {
        u8 buf[BUN_ASSET_RECORD_SIZE];
        bun_result_t rr = read_record_bytes(ctx, header, i, buf);
        if (rr == BUN_ERR_IO) return BUN_ERR_IO;
        if (rr != BUN_OK)     { escalate(&worst, rr); continue; }

        decode_record(buf, &ctx->records[i]);

        bun_result_t status = BUN_OK;
        check_data_bounds       (ctx, header, i, &ctx->records[i], &status);
        check_compression       (ctx,         i, &ctx->records[i], &status);
        check_uncompressed_size (ctx,         i, &ctx->records[i], &status);
        check_rle_parity        (ctx,         i, &ctx->records[i], &status);
        check_checksum          (ctx,         i, &ctx->records[i], &status);
        check_flags             (ctx,         i, &ctx->records[i], &status);
        escalate(&worst, status);
    }

    return worst;
}

