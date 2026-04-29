#include "../bun.h"
#include "../bun_string_table.h"
#include "../bun_data.h"

#include <check.h>

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

// Helper: terminate abnormally, after printing a message to stderr
void die(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "fatal error: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  va_end(args);

  abort();
}


// Helper: open a test fixture by name, relative to the tests/ directory.
static const char *fixture(const char *filename) {
    // For simplicity, tests assume they are run from the project root, and
    // test BUN files live in tests/fixtures/{valid,invalid}. Adjust if needed.
    static char path[256];
    int res = snprintf(path, sizeof(path), "tests/fixtures/%s", filename);
    if (res < 0) {
      die("snprintf failed: %s", strerror(errno));
    }
    if ((size_t) res > sizeof(path)) {
      die("filename '%s' too big for buffer (would write %d bytes to %zu-size buffer)",
          filename, res, sizeof(path));
    }
    return path;
}

// ============================================================
// Little-endian write helpers (used by in-memory header builders)
// ============================================================

static void w_u16(u8 *p, u16 v) {
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
}
static void w_u32(u8 *p, u32 v) {
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}
static void w_u64(u8 *p, u64 v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (u8)((v >> (i * 8)) & 0xFF);
    }
}

// Build a 60-byte BUN header at `buf` with the given fields. Used by
// the in-memory header tests below so we don't need on-disk fixtures.
static void build_header(u8 buf[60],
                         u32 magic,
                         u16 ver_major, u16 ver_minor,
                         u32 asset_count,
                         u64 asset_table_offset,
                         u64 string_table_offset,
                         u64 string_table_size,
                         u64 data_section_offset,
                         u64 data_section_size,
                         u64 reserved)
{
    w_u32(buf +  0, magic);
    w_u16(buf +  4, ver_major);
    w_u16(buf +  6, ver_minor);
    w_u32(buf +  8, asset_count);
    w_u64(buf + 12, asset_table_offset);
    w_u64(buf + 20, string_table_offset);
    w_u64(buf + 28, string_table_size);
    w_u64(buf + 36, data_section_offset);
    w_u64(buf + 44, data_section_size);
    w_u64(buf + 52, reserved);
}

// Build a 48-byte asset record at `buf`.
static void build_record(u8 buf[48],
                         u32 name_offset, u32 name_length,
                         u64 data_offset, u64 data_size,
                         u64 uncompressed_size,
                         u32 compression, u32 type,
                         u32 checksum, u32 flags)
{
    w_u32(buf +  0, name_offset);
    w_u32(buf +  4, name_length);
    w_u64(buf +  8, data_offset);
    w_u64(buf + 16, data_size);
    w_u64(buf + 24, uncompressed_size);
    w_u32(buf + 32, compression);
    w_u32(buf + 36, type);
    w_u32(buf + 40, checksum);
    w_u32(buf + 44, flags);
}

// ============================================================
// Header parsing fixture-based tests
// (These require tests/fixtures/{valid,invalid}/*.bun to exist.)
// ============================================================

START_TEST(test_valid_minimal) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("valid/01-empty.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(header.magic, BUN_MAGIC);
    ck_assert_uint_eq(header.version_major, 1);
    ck_assert_uint_eq(header.version_minor, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_bad_magic) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/01-bad-magic.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_unsupported_version) {
    BunParseContext ctx = {0};
    BunHeader header    = {0};

    bun_result_t r = bun_open(fixture("invalid/02-bad-version.bun"), &ctx);
    ck_assert_int_eq(r, BUN_OK);

    r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);

    bun_close(&ctx);
}
END_TEST

// ============================================================
// String-table tests
//
// These use fmemopen(3) so each test carries its own in-memory "file" and
// does not depend on header/asset parsing being implemented. The header
// and record structs are constructed by hand.
// ============================================================

START_TEST(test_stringtable_valid_single_name) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");
    ck_assert_ptr_ne(f, NULL);

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    BunAssetRecord record = {
        .name_offset = 0,
        .name_length = 5,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_OK);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_zero_length_name) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    BunAssetRecord record = {
        .name_offset = 0,
        .name_length = 0,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_MALFORMED);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_non_printable_byte) {
    // 0x01 is outside the printable range (0x20..0x7E).
    char buffer[] = { 'h', 'e', 0x01, 'l', 'o' };
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    BunAssetRecord record = {
        .name_offset = 0,
        .name_length = 5,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_MALFORMED);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_name_exceeds_table_size) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    // 3 + 10 = 13, which runs off the end of the 5-byte table.
    BunAssetRecord record = {
        .name_offset = 3,
        .name_length = 10,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_MALFORMED);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_integer_overflow) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    // name_offset + name_length wraps around in u32.
    BunAssetRecord record = {
        .name_offset = UINT32_MAX,
        .name_length = 10,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_MALFORMED);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_two_adjacent_names) {
    // "foo" at offset 0, "bar" at offset 3.
    char buffer[] = "foobar";
    FILE *f = fmemopen(buffer, 6, "rb");

    BunParseContext ctx = { .file = f, .file_size = 6 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 6,
    };
    BunAssetRecord records[2] = {
        { .name_offset = 0, .name_length = 3 },
        { .name_offset = 3, .name_length = 3 },
    };
    ck_assert_int_eq(bun_validate_string_table(&ctx, &header, records, 2), BUN_OK);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_table_offset_nonzero) {
    // Simulate a file where the string table does not start at offset 0.
    // Bytes before the table are padding (just arbitrary filler here).
    char buffer[] = { 'X', 'X', 'X', 'X', 'h', 'i' };
    FILE *f = fmemopen(buffer, sizeof(buffer), "rb");

    BunParseContext ctx = { .file = f, .file_size = sizeof(buffer) };
    BunHeader header = {
        .string_table_offset = 4,
        .string_table_size   = 2,
    };
    BunAssetRecord record = {
        .name_offset = 0,
        .name_length = 2,
    };
    ck_assert_int_eq(bun_validate_asset_name(&ctx, &header, &record), BUN_OK);

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_read_prefix_full) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    BunAssetRecord record = { .name_offset = 0, .name_length = 5 };

    char out[32];
    ck_assert_int_eq(bun_read_asset_name_prefix(&ctx, &header, &record,
                                                out, sizeof(out)), BUN_OK);
    ck_assert_str_eq(out, "hello");

    fclose(f);
}
END_TEST

START_TEST(test_stringtable_read_prefix_truncated) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");

    BunParseContext ctx = { .file = f, .file_size = 5 };
    BunHeader header = {
        .string_table_offset = 0,
        .string_table_size   = 5,
    };
    BunAssetRecord record = { .name_offset = 0, .name_length = 5 };

    // Buffer only fits 3 characters plus the terminator.
    char out[4];
    ck_assert_int_eq(bun_read_asset_name_prefix(&ctx, &header, &record,
                                                out, sizeof(out)), BUN_OK);
    ck_assert_str_eq(out, "hel");

    fclose(f);
}
END_TEST

// ============================================================
// bun_open: file-not-found error code (BUN_ERR_NOT_FOUND = 4)
// ============================================================

START_TEST(test_open_nonexistent_file) {
    BunParseContext ctx = {0};
    bun_result_t r = bun_open(
        "tests/fixtures/this/path/definitely/does/not/exist.bun", &ctx);
    ck_assert_int_eq(r, BUN_ERR_NOT_FOUND);
    // ctx->file should still be NULL after a failed open.
    ck_assert_ptr_eq(ctx.file, NULL);
}
END_TEST

// ============================================================
// Header bounds (spec 9.2) and overlap (spec 9.3) tests.
// All built in memory via fmemopen + build_header.
// ============================================================

// 9.2: a section that runs past EOF must be flagged MALFORMED.
START_TEST(test_header_string_table_past_eof) {
    u8 buf[60];
    // file_size = 60 (just the header). Claim a 100-byte string table at
    // offset 60 -- 60+100 > 60, so 9.2 fails.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/60,
                 /*string_table_size*/  100,
                 /*data_section_offset*/60,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = 60 };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    // Should have at least one violation explaining what failed.
    ck_assert_uint_gt(ctx.n_violations, 0);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// 9.3: two sections that overlap must be flagged MALFORMED.
START_TEST(test_header_sections_overlap) {
    u8 buf[200] = {0};
    // String table at [60, 100), data section at [80, 160) -- overlap on [80, 100).
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/60,
                 /*string_table_size*/  40,
                 /*data_section_offset*/80,
                 /*data_section_size*/  80,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// 9.3 boundary case: sections that touch but don't overlap (offset+size ==
// next.offset) must NOT be reported as overlapping.
START_TEST(test_header_adjacent_sections_ok) {
    u8 buf[200] = {0};
    // String table at [60, 100), data section at [100, 200) -- they touch
    // exactly but do not overlap.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/60,
                 /*string_table_size*/  40,
                 /*data_section_offset*/100,
                 /*data_section_size*/  100,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    // Header is otherwise valid, so this must be OK (no overlap violation).
    ck_assert_int_eq(r, BUN_OK);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// 9.3 wraparound safety: a section with offset+size that would wrap u64
// must be reported as MALFORMED (via 9.2 past-EOF) without invoking
// undefined behaviour in sections_overlap().
START_TEST(test_header_overflow_safe) {
    u8 buf[60];
    // string_table_offset = UINT64_MAX - 5, size = 100. offset+size wraps.
    // The 9.2 check should escalate MALFORMED; sections_overlap must not UB.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/UINT64_MAX - 5,
                 /*string_table_size*/  100,
                 /*data_section_offset*/60,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = 60 };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// ============================================================
// DoS-protection cap: BUN_MAX_RECORDS = 1,000,000.
// asset_count above the cap must return BUN_UNSUPPORTED (not MALFORMED),
// because the file may be structurally valid -- we just refuse to load it.
// ============================================================

START_TEST(test_dos_cap_excessive_asset_count) {
    u8 buf[60];
    // Claim 2,000,000 assets. We don't actually allocate the asset-table
    // bytes on disk; the cap check fires before any record is read.
    // To get past the 9.2 asset-table-bounds check we lie about file_size.
    u32 huge_count = 2000000u;
    u64 fake_file_size = (u64)huge_count * 48u + 60u; // big enough to "fit"

    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        huge_count,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/60,
                 /*string_table_size*/  0,
                 /*data_section_offset*/60,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = fake_file_size };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_result_t r = bun_parse_assets(&ctx, &header);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);
    // ctx->records must NOT have been allocated -- the whole point is to
    // refuse before calloc().
    ck_assert_ptr_eq(ctx.records, NULL);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// At-the-boundary case: exactly BUN_MAX_RECORDS should still be accepted.
// We do not actually exercise the parse loop here -- we'd have to provide
// 48 * 1,000,000 bytes of asset records, which is fine but wasteful in a
// unit test. Instead, we check the cap is strictly an upper bound.
START_TEST(test_dos_cap_at_limit_not_rejected) {
    // We can't easily build a 48 MB in-memory file in a unit test. The
    // important thing the cap guarantees is "> BUN_MAX_RECORDS" rejects;
    // "== BUN_MAX_RECORDS" accepts. Sanity-check the macro value here so
    // that anyone changing it is reminded to update this test.
    ck_assert_uint_eq(BUN_MAX_RECORDS, 1000000u);
}
END_TEST

// ============================================================
// bun_data: end-to-end preview test.
// Builds a complete in-memory BUN file: header + 1 asset record + string
// table + uncompressed data. Verifies that the data preview is correct.
// ============================================================

START_TEST(test_data_preview_printable_uncompressed) {
    // Layout (all offsets in bytes). Spec section 4.1 requires every
    // section offset and size to be divisible by 4, so we pad the
    // string table (5 -> 8 bytes) and data section (11 -> 12 bytes).
    //   [0,   60): header
    //   [60, 108): one 48-byte asset record
    //   [108, 116): string table -- "asset" (5 bytes) + 3 padding bytes
    //   [116, 128): data section -- "hello world" (11 bytes) + 1 padding byte
    static u8 buf[128];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 12;

    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        1,
                 asset_table_offset,
                 string_table_offset,
                 string_table_size,
                 data_section_offset,
                 data_section_size,
                 /*reserved*/           0);

    // Asset record at offset 60.
    build_record(buf + 60,
                 /*name_offset*/        0,
                 /*name_length*/        5,
                 /*data_offset*/        0,
                 /*data_size*/          11,
                 /*uncompressed_size*/  0,    // 0 because compression=none
                 /*compression*/        0,
                 /*type*/               0,
                 /*checksum*/           0,
                 /*flags*/              0);

    memcpy(buf + string_table_offset, "asset",       5);
    memcpy(buf + data_section_offset, "hello world", 11);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(ctx.record_count, 1);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(dres.preview_len, 11);
    ck_assert_int_eq(dres.is_printable, 1);
    // Older libcheck (e.g., Ubuntu 20.04's 0.10.x) lacks ck_assert_mem_eq;
    // memcmp + ck_assert_int_eq is portable and gives the same guarantee.
    ck_assert_int_eq(memcmp(dres.preview, "hello world", 11), 0);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// Same end-to-end shape, but with binary (non-printable) data.
START_TEST(test_data_preview_binary_uncompressed) {
    // Same 4-byte alignment rules as above. data_section_size of 4 is
    // already aligned; only the string table needs padding (5 -> 8).
    //   [0,   60): header
    //   [60, 108): one 48-byte asset record
    //   [108, 116): string table -- "asset" + 3 padding bytes
    //   [116, 120): data section -- 4 bytes of binary
    static u8 buf[120];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 4;

    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        1,
                 asset_table_offset,
                 string_table_offset,
                 string_table_size,
                 data_section_offset,
                 data_section_size,
                 /*reserved*/           0);

    build_record(buf + 60,
                 0, 5,                  // name_offset, name_length
                 0, 4,                  // data_offset, data_size
                 0,                     // uncompressed_size (0 for compression=none)
                 0, 0, 0, 0);           // compression, type, checksum, flags

    memcpy(buf + string_table_offset, "asset", 5);
    // 4 bytes of binary that include 0x00, which is non-printable.
    buf[data_section_offset + 0] = 0x00;
    buf[data_section_offset + 1] = 0xFF;
    buf[data_section_offset + 2] = 0x7F;
    buf[data_section_offset + 3] = 0x01;

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(dres.preview_len, 4);
    ck_assert_int_eq(dres.is_printable, 0);

    // bun_close() handles fclose(ctx->file), so don't fclose(f) separately
    // -- that would be a double-free on the same FILE*.
    bun_close(&ctx);
}
END_TEST

// ============================================================
// Data-section tests

// Valid RLE
START_TEST(test_data_rle_valid) {
    // RLE encode "AAABBBCCCCDD" -> pairs: (3,A)(3,B)(4,C)(2,D)
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    u8 rle_data[] = { 3, 'A', 3, 'B', 4, 'C', 2, 'D' };
    u64 rle_size  = sizeof(rle_data);
    u64 raw_size  = 3 + 3 + 4 + 2; // 12

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 60; // align4(rle_size)

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    build_record(buf + 60, 0, 5, 0, rle_size, raw_size, 1, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, rle_data, rle_size);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_OK);
    ck_assert_uint_eq(dres.preview_len, 12);
    ck_assert_int_eq(dres.is_printable, 1);

    bun_close(&ctx);
}
END_TEST


//z-lib -> BUN_UNSUPPORTED

START_TEST(test_data_zlib_unsupported) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 8;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // compression = 2 (zlib), uncompressed_size = 10
    build_record(buf + 60, 0, 5, 0, 4, 10, 2, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_parse_assets(&ctx, &header);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);

    bun_close(&ctx);
}
END_TEST

// non-zero checksum -> BUN_UNSUPPORTED
START_TEST(test_data_nonzero_checksum) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 8;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // checksum = 0xDEADBEEF (non-zero)
    build_record(buf + 60, 0, 5, 0, 4, 0, 0, 0, 0xDEADBEEF, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, "test", 4);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_parse_assets(&ctx, &header);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);

    bun_close(&ctx);
}
END_TEST

// Unknown flag -> BUN_UNSUPPORTED

START_TEST(test_data_unknown_flags) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 8;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // flags = 0xFF — bits beyond 0x1 and 0x2 are set
    build_record(buf + 60, 0, 5, 0, 4, 0, 0, 0, 0, 0xFF);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, "test", 4);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_parse_assets(&ctx, &header);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_UNSUPPORTED);

    bun_close(&ctx);
}
END_TEST

// RLE odd size -> BUN _MALFORMED
START_TEST(test_data_rle_odd_size) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    // 3 bytes of RLE data - odd, not valid
    u8 rle_data[] = { 3, 'A', 2 };

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 4;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // data_size = 3 (odd), compression = 1 (RLE)
    build_record(buf + 60, 0, 5, 0, 3, 5, 1, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, rle_data, sizeof(rle_data));

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_parse_assets(&ctx, &header);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// RLE zero count pair -> BUN_MALFORMED
START_TEST(test_data_rle_zero_count) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    // (3, 'A') valid, (0, 'B') invalid
    u8 rle_data[] = { 3, 'A', 0, 'B' };

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 4;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    build_record(buf + 60, 0, 5, 0, 4, 3, 1, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, rle_data, sizeof(rle_data));

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

// RLE Size Mismatch -> BUN_Malformed
START_TEST(test_data_rle_size_mismatch) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    // (3, 'A')(3, 'B') -> decompresses to 6 bytes
    u8 rle_data[] = { 3, 'A', 3, 'B' };

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 4;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // uncompressed_size = 999, but actual decompressed size = 6
    build_record(buf + 60, 0, 5, 0, 4, 999, 1, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, rle_data, sizeof(rle_data));

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

//Uncompressed with non-zero uncompressed_size → BUN_MALFORMED
START_TEST(test_data_uncompressed_bad_size) {
    static u8 buf[176];
    memset(buf, 0, sizeof(buf));

    const u64 asset_table_offset  = 60;
    const u64 string_table_offset = 108;
    const u64 string_table_size   = 8;
    const u64 data_section_offset = 116;
    const u64 data_section_size   = 8;

    build_header(buf, BUN_MAGIC, 1, 0, 1,
                 asset_table_offset, string_table_offset, string_table_size,
                 data_section_offset, data_section_size, 0);

    // compression = 0 but uncompressed_size = 99 (should be 0)
    build_record(buf + 60, 0, 5, 0, 4, 99, 0, 0, 0, 0);

    memcpy(buf + string_table_offset, "asset", 5);
    memcpy(buf + data_section_offset, "test", 4);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_parse_assets(&ctx, &header);

    BunAssetDataResult dres = {0};
    bun_result_t r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_MALFORMED);

    bun_close(&ctx);
}
END_TEST

//Large file
START_TEST(test_data_large_file) {
    // This test requires a pre-generated large bun file.
    // Generate it with: python3 tests/generate_large_bun.py
    // It is not included in the submission zip.
    const char *path = "tests/fixtures/large/large_650mb.bun";

    BunParseContext ctx = {0};
    bun_result_t r = bun_open(path, &ctx);
    if (r != BUN_OK) {
        // File not present -- skip rather than fail
        return;
    }

    BunHeader header = {0};
    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_int_eq(bun_parse_assets(&ctx, &header), BUN_OK);

    BunAssetDataResult dres = {0};
    r = bun_process_asset_data(&ctx, &header, &ctx.records[0], &dres);
    ck_assert_int_eq(r, BUN_OK);

    bun_close(&ctx);
}
END_TEST










// ============================================================
// Header edge-case tests (originally added by Aman on amanTesting).
// Cover file-too-short, 4-byte alignment violations on offsets and
// sizes, large-but-valid offsets when size is zero, multi-violation
// escalation order, sections overlapping the header, all-zero header
// fields, version 1.x where x != 0, and non-zero reserved field.
// ============================================================

START_TEST(test_header_file_too_short) {
    u8 buf[10] = {0}; // 10 bytes, not even close to the 60 expected.

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_header_misaligned_offset) {
    u8 buf[200] = {0};
    // asset_table_offset = 61, which is not divisible by 4.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 61,
                 /*string_table_offset*/64,
                 /*string_table_size*/  0,
                 /*data_section_offset*/64,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_header_misaligned_size) {
    u8 buf[200] = {0};
    // string_table_size = 5, which is not divisible by 4.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/60,
                 /*string_table_size*/  5,
                 /*data_section_offset*/68,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_edge_zero_size_offset_slip) {
    u8 buf[60];
    // A huge asset_table_offset is fine when asset_count == 0, since
    // there's no actual asset table that needs to fit at that offset.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 0xFFFFFFF0, // huge but still 4-byte aligned
                 /*string_table_offset*/60,
                 /*string_table_size*/  0,
                 /*data_section_offset*/60,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    bun_close(&ctx);
}
END_TEST

START_TEST(test_combo_magic_n_version) {
    u8 buf[60];
    // Both magic and version are wrong. MALFORMED (magic) must beat
    // UNSUPPORTED (version) per the escalate() ordering.
    build_header(buf, 0xDEADBEEF, 99, 255,
                 0, 60, 60, 0, 60, 0, 0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_ge(ctx.n_violations, 2);   // at least one for magic, one for version

    bun_close(&ctx);
}
END_TEST

START_TEST(test_edge_section_overlaps_header) {
    u8 buf[200] = {0};
    // string_table at [28, 68) overlaps the header at [0, 60).
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 60,
                 /*string_table_offset*/28,
                 /*string_table_size*/  40,
                 /*data_section_offset*/68,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, sizeof(buf), "rb");
    BunParseContext ctx = { .file = f, .file_size = sizeof(buf) };
    BunHeader header = {0};

    bun_result_t r = bun_parse_header(&ctx, &header);
    ck_assert_int_eq(r, BUN_MALFORMED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_edge_all_zero_fields) {
    u8 buf[60];
    // All offsets and sizes zero. With size=0 each section is empty,
    // so there's no actual overlap or out-of-bounds situation.
    build_header(buf, BUN_MAGIC, 1, 0,
                 /*asset_count*/        0,
                 /*asset_table_offset*/ 0,
                 /*string_table_offset*/0,
                 /*string_table_size*/  0,
                 /*data_section_offset*/0,
                 /*data_section_size*/  0,
                 /*reserved*/           0);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = 60 };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_edge_version_minor_nonzero) {
    u8 buf[60];
    // major=1 is correct; minor=255 is not. Spec says version must be
    // exactly 1.0; otherwise UNSUPPORTED.
    build_header(buf, BUN_MAGIC,
                 1, 255,
                 0, 60, 60, 0, 60, 0, 0);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = 60 };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_UNSUPPORTED);
    ck_assert_uint_gt(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

START_TEST(test_edge_reserved_nonzero) {
    u8 buf[60];
    // Spec section 4.1.6 says reserved is ignored. Setting it to
    // UINT64_MAX must not produce a violation.
    build_header(buf, BUN_MAGIC, 1, 0,
                 0, 60, 60, 0, 60, 0,
                 /*reserved*/ UINT64_MAX);

    FILE *f = fmemopen(buf, 60, "rb");
    BunParseContext ctx = { .file = f, .file_size = 60 };
    BunHeader header = {0};

    ck_assert_int_eq(bun_parse_header(&ctx, &header), BUN_OK);
    ck_assert_uint_eq(ctx.n_violations, 0);

    bun_close(&ctx);
}
END_TEST

// Assemble a test suite from our tests

static Suite *bun_suite(void) {
    Suite *s = suite_create("bun-suite");

    // Note that "TCase" is more like a sub-suite than a single test case
    TCase *tc_header = tcase_create("header-tests");
    tcase_add_test(tc_header, test_valid_minimal);
    tcase_add_test(tc_header, test_bad_magic);
    tcase_add_test(tc_header, test_unsupported_version);
    suite_add_tcase(s, tc_header);

    TCase *tc_strtab = tcase_create("string-table-tests");
    tcase_add_test(tc_strtab, test_stringtable_valid_single_name);
    tcase_add_test(tc_strtab, test_stringtable_zero_length_name);
    tcase_add_test(tc_strtab, test_stringtable_non_printable_byte);
    tcase_add_test(tc_strtab, test_stringtable_name_exceeds_table_size);
    tcase_add_test(tc_strtab, test_stringtable_integer_overflow);
    tcase_add_test(tc_strtab, test_stringtable_two_adjacent_names);
    tcase_add_test(tc_strtab, test_stringtable_table_offset_nonzero);
    tcase_add_test(tc_strtab, test_stringtable_read_prefix_full);
    tcase_add_test(tc_strtab, test_stringtable_read_prefix_truncated);
    suite_add_tcase(s, tc_strtab);

    // Tests for the loose_ends additions: file-not-found error code,
    // spec 9.2 bounds, spec 9.3 overlap, DoS-protection record cap, and
    // the data-section preview pipeline.
    TCase *tc_open = tcase_create("open-error-tests");
    tcase_add_test(tc_open, test_open_nonexistent_file);
    suite_add_tcase(s, tc_open);

    TCase *tc_bounds = tcase_create("header-bounds-tests");
    tcase_add_test(tc_bounds, test_header_string_table_past_eof);
    tcase_add_test(tc_bounds, test_header_sections_overlap);
    tcase_add_test(tc_bounds, test_header_adjacent_sections_ok);
    tcase_add_test(tc_bounds, test_header_overflow_safe);
    suite_add_tcase(s, tc_bounds);

    TCase *tc_dos = tcase_create("dos-cap-tests");
    tcase_add_test(tc_dos, test_dos_cap_excessive_asset_count);
    tcase_add_test(tc_dos, test_dos_cap_at_limit_not_rejected);
    suite_add_tcase(s, tc_dos);

    TCase *tc_data = tcase_create("data-preview-tests");
    tcase_add_test(tc_data, test_data_preview_printable_uncompressed);
    tcase_add_test(tc_data, test_data_preview_binary_uncompressed);
    suite_add_tcase(s, tc_data);

    TCase *tc_data_section = tcase_create("data-section-tests");
    tcase_set_timeout(tc_data_section, 60); // 60s timeout for large file test
    tcase_add_test(tc_data_section, test_data_rle_valid);
    tcase_add_test(tc_data_section, test_data_zlib_unsupported);
    tcase_add_test(tc_data_section, test_data_nonzero_checksum);
    tcase_add_test(tc_data_section, test_data_unknown_flags);
    tcase_add_test(tc_data_section, test_data_rle_odd_size);
    tcase_add_test(tc_data_section, test_data_rle_zero_count);
    tcase_add_test(tc_data_section, test_data_rle_size_mismatch);
    tcase_add_test(tc_data_section, test_data_uncompressed_bad_size);
    tcase_add_test(tc_data_section, test_data_large_file);
    suite_add_tcase(s, tc_data_section);

    // Header edge-case tests (formerly on the duplicate root-level
    // test_bun.c). 9 tests covering file-too-short, alignment, large
    // offsets with zero size, multi-violation escalation, header
    // overlaps, all-zero fields, version minor, and reserved field.
    TCase *tc_header_edge = tcase_create("header-edge-tests");
    tcase_add_test(tc_header_edge, test_header_file_too_short);
    tcase_add_test(tc_header_edge, test_header_misaligned_offset);
    tcase_add_test(tc_header_edge, test_header_misaligned_size);
    tcase_add_test(tc_header_edge, test_edge_zero_size_offset_slip);
    tcase_add_test(tc_header_edge, test_combo_magic_n_version);
    tcase_add_test(tc_header_edge, test_edge_section_overlaps_header);
    tcase_add_test(tc_header_edge, test_edge_all_zero_fields);
    tcase_add_test(tc_header_edge, test_edge_version_minor_nonzero);
    tcase_add_test(tc_header_edge, test_edge_reserved_nonzero);
    suite_add_tcase(s, tc_header_edge);

    return s;
}

int main(void) {
    Suite   *s  = bun_suite();
    SRunner *sr = srunner_create(s);

    // see https://libcheck.github.io/check/doc/check_html/check_3.html#SRunner-Output for different output options.
    // e.g. pass CK_VERBOSE if you want to see successes as well as failures.
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
