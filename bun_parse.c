#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <assert.h>

#include "bun.h"

//
// Little-endian read helpers 
//

static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

static u64 read_u64_le(const u8 *buf, size_t offset) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (u64)buf[offset + i] << (i * 8);
    }
    return v;
}

//
// Violation reporting 
//

static void add_violation(BunParseContext *ctx, const char *msg) {
    if (ctx->n_violations == ctx->cap_violations) {
        size_t ncap = ctx->cap_violations ? ctx->cap_violations * 2 : 8;
        char **nv = realloc(ctx->violations, ncap * sizeof(char *));
        if (!nv) return;  
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

// ================================================================
// --- HEADER SECTION  ---
// ================================================================

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
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
  ctx->file_size = (long)sz;     //consider widening to u64 later
  rewind(ctx->file);

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];

  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    return BUN_MALFORMED;
  }

  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    return BUN_ERR_IO;
  }

  // TODO : populate `header` from `buf`.
  // TODO : validate fields and return BUN_MALFORMED / BUN_UNSUPPORTED.

  if (header->magic != BUN_MAGIC) {
    return BUN_MALFORMED;
  }

  return BUN_OK;
}

// ================================================================
// --- Asset Entry Table  ---
// ================================================================

/*
 * Decode one 48-byte asset record from a byte buffer.
 * Offsets match spec §5 exactly.
 */
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

/*
 * Seek safely to a u64 position using fseeko/off_t.
 * Returns -1 if the position would overflow off_t or if the seek fails.
 */
static int safe_seek(FILE *f, u64 pos) {
    if (pos > (u64)INT64_MAX) return -1;
    return fseeko(f, (off_t)pos, SEEK_SET);
}

/*
 * Escalate an error state. We keep the worst result seen so far:
 *   BUN_MALFORMED always wins (overrides both OK and UNSUPPORTED).
 *   BUN_UNSUPPORTED overrides OK only.
 */
static void escalate(bun_result_t *status, bun_result_t new_status) {
    if (new_status == BUN_MALFORMED) {
        *status = BUN_MALFORMED;
    } else if (new_status == BUN_UNSUPPORTED && *status == BUN_OK) {
        *status = BUN_UNSUPPORTED;
    }
}

static void check_name_bounds(BunParseContext *ctx, const BunHeader *h,
                              u32 i, const BunAssetRecord *r,
                              bun_result_t *status) {
    if (r->name_length == 0) {
        add_violationf(ctx, "asset[%u]: name_length is zero", i);
        escalate(status, BUN_MALFORMED);
        return;
    }
    // Widen u32 to u64 before addition to prevent wraparound.
    u64 end = (u64)r->name_offset + (u64)r->name_length;
    if (end > h->string_table_size) {
        add_violationf(ctx,
            "asset[%u]: name range [%u+%u=%llu] exceeds string_table_size %llu",
            i, r->name_offset, r->name_length,
            (unsigned long long)end,
            (unsigned long long)h->string_table_size);
        escalate(status, BUN_MALFORMED);
    }
}
static void check_data_bounds(BunParseContext *ctx, const BunHeader *h,
                              u32 i, const BunAssetRecord *r,
                              bun_result_t *status) {
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

static void check_compression(BunParseContext *ctx, u32 i,
                              const BunAssetRecord *r, bun_result_t *status) {
    if (r->compression == 0 || r->compression == 1) return;
    if (r->compression == 2) {
        add_violationf(ctx, "asset[%u]: zlib compression not supported", i);
    } else {
        add_violationf(ctx, "asset[%u]: unknown compression value %u",
                       i, r->compression);
    }
    escalate(status, BUN_UNSUPPORTED);
}

static void check_uncompressed_size(BunParseContext *ctx, u32 i,
                                    const BunAssetRecord *r,
                                    bun_result_t *status) {
    if (r->compression == 0 && r->uncompressed_size != 0) {
        add_violationf(ctx,
            "asset[%u]: compression=none but uncompressed_size=%llu "
            "(must be 0 per spec §5.1 note 1)",
            i, (unsigned long long)r->uncompressed_size);
        escalate(status, BUN_MALFORMED);
    }
}

/*
 * RLE data consists of (count, byte) pairs, so the on-disk size
 * must be even. Contents are validated by the data-section code.
 */
static void check_rle_parity(BunParseContext *ctx, u32 i,
                             const BunAssetRecord *r, bun_result_t *status) {
    if (r->compression == 1 && (r->data_size % 2) != 0) {
        add_violationf(ctx,
            "asset[%u]: RLE data_size %llu is not even (spec §8.2)",
            i, (unsigned long long)r->data_size);
        escalate(status, BUN_MALFORMED);
    }
}

static void check_checksum(BunParseContext *ctx, u32 i,
                           const BunAssetRecord *r, bun_result_t *status) {
    if (r->checksum != 0) {
        add_violationf(ctx,
            "asset[%u]: non-zero checksum 0x%08x "
            "(CRC-32 validation is an optional feature we do not support)",
            i, r->checksum);
        escalate(status, BUN_UNSUPPORTED);
    }
}

static void check_flags(BunParseContext *ctx, u32 i,
                        const BunAssetRecord *r, bun_result_t *status) {
    u32 known = BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE;
    u32 unknown = r->flags & ~known;
    if (unknown != 0) {
        add_violationf(ctx,
            "asset[%u]: unknown flag bits set (flags=0x%08x, unknown=0x%08x)",
            i, r->flags, unknown);
        escalate(status, BUN_UNSUPPORTED);
    }
}

/*
 * Read one asset record's bytes from disk with full bounds checking.
 * Uses fseeko/off_t so it works on files larger than 2 GB.
 */
static bun_result_t read_record_bytes(BunParseContext *ctx,
                                      const BunHeader *header,
                                      u32 index,
                                      u8 buf[BUN_ASSET_RECORD_SIZE]) {
    u64 row = (u64)index * (u64)BUN_ASSET_RECORD_SIZE;
    if (header->asset_table_offset > UINT64_MAX - row) {
        add_violationf(ctx, "asset[%u]: record offset overflows u64", index);
        return BUN_MALFORMED;
    }
    u64 pos = header->asset_table_offset + row;

    if (pos > UINT64_MAX - BUN_ASSET_RECORD_SIZE ||
        pos + BUN_ASSET_RECORD_SIZE > (u64)ctx->file_size) {
        add_violationf(ctx,
            "asset[%u]: record would read past end of file (pos=%llu, size=%lld)",
            index, (unsigned long long)pos, (long long)ctx->file_size);
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

    // asset_count is u32 (max ~4.3e9) and BUN_ASSET_RECORD_SIZE is 48,
    // so the product fits in u64 without overflow. No guard needed.
    u64 table_bytes = (u64)header->asset_count * (u64)BUN_ASSET_RECORD_SIZE;

    if (header->asset_table_offset > UINT64_MAX - table_bytes) {
        add_violation(ctx, "asset table end overflows u64");
        return BUN_MALFORMED;
    }
    if (header->asset_table_offset + table_bytes > (u64)ctx->file_size) {
        add_violation(ctx, "asset table extends past end of file");
        return BUN_MALFORMED;
    }

    if (header->asset_count == 0) return BUN_OK;

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
        check_name_bounds       (ctx, header, i, &ctx->records[i], &status);
        check_data_bounds       (ctx, header, i, &ctx->records[i], &status);
        check_compression       (ctx,          i, &ctx->records[i], &status);
        check_uncompressed_size (ctx,          i, &ctx->records[i], &status);
        check_rle_parity        (ctx,          i, &ctx->records[i], &status);
        check_checksum          (ctx,          i, &ctx->records[i], &status);
        check_flags             (ctx,          i, &ctx->records[i], &status);
        escalate(&worst, status);
    }

    return worst;
}

// ================================================================
// --- CLEANUP ---
// ================================================================

bun_result_t bun_close(BunParseContext *ctx) {
    assert(ctx->file);

    if (ctx->names) {
        for (size_t i = 0; i < ctx->record_count; i++) {
            free(ctx->names[i]);
        }
        free(ctx->names);
        ctx->names = NULL;
    }
    free(ctx->records);
    ctx->records = NULL;
    ctx->record_count = 0;

    for (size_t i = 0; i < ctx->n_violations; i++) free(ctx->violations[i]);
    free(ctx->violations);
    ctx->violations = NULL;
    ctx->n_violations = ctx->cap_violations = 0;

    int res = fclose(ctx->file);
    ctx->file = NULL;
    return res ? BUN_ERR_IO : BUN_OK;
}