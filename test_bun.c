#include "../bun.h"
#include "../bun_string_table.h"

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

// Example test suite: header parsing

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

// String-table tests.
//
// These use fmemopen(3) so each test carries its own in-memory "file" and
// does not depend on header/asset parsing being implemented. The header
// and record structs are constructed by hand.

START_TEST(test_stringtable_valid_single_name) {
    char buffer[] = "hello";
    FILE *f = fmemopen(buffer, 5, "rb");
    ck_assert_ptr_nonnull(f);

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

    BunParseContext ctx = { .file = f, .file_size = (long)sizeof(buffer) };
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

    // TODO: add further test cases and TCases (e.g. "assets", "compression")

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

