#include <stdio.h>
#include <stdlib.h>

#include "bun.h"
#include "bun_string_table.h"
#include "bun_data.h"

// Print all violations collected so far (used on every exit path so the
// user sees what went wrong, regardless of which stage failed).
static void print_violations(const BunParseContext *ctx) {
    for (size_t i = 0; i < ctx->n_violations; i++) {
        fprintf(stderr, "violation: %s\n", ctx->violations[i]);
    }
}

static void print_header(const BunHeader *h) {
    printf("=== BUN Header ===\n");
    printf("magic                = 0x%08X\n", h->magic);
    printf("version              = %u.%u\n",  h->version_major, h->version_minor);
    printf("asset_count          = %u\n",     h->asset_count);
    printf("asset_table_offset   = %llu\n",   (unsigned long long)h->asset_table_offset);
    printf("string_table_offset  = %llu\n",   (unsigned long long)h->string_table_offset);
    printf("string_table_size    = %llu\n",   (unsigned long long)h->string_table_size);
    printf("data_section_offset  = %llu\n",   (unsigned long long)h->data_section_offset);
    printf("data_section_size    = %llu\n",   (unsigned long long)h->data_section_size);
    printf("reserved             = %llu\n",   (unsigned long long)h->reserved);
}

static bun_result_t print_asset(size_t i, BunParseContext *ctx,
                                const BunHeader *header,
                                const BunAssetRecord *r,
                                char *const *names, size_t name_count) {
    printf("\n=== Asset %zu ===\n", i);

    const char *name = "(name unreadable)";
    if (names && i < name_count && names[i]) name = names[i];
    printf("  name              = %s\n", name);

    printf("  name_offset       = %u\n",   r->name_offset);
    printf("  name_length       = %u\n",   r->name_length);
    printf("  data_offset       = %llu\n", (unsigned long long)r->data_offset);
    printf("  data_size         = %llu\n", (unsigned long long)r->data_size);
    printf("  uncompressed_size = %llu\n", (unsigned long long)r->uncompressed_size);
    printf("  compression       = %u\n",   r->compression);
    printf("  type              = %u\n",   r->type);
    printf("  checksum          = 0x%08x\n", r->checksum);
    printf("  flags             = 0x%08x\n", r->flags);

    // Process + preview the data section for this asset.
    // Brief 5.2d: print as much as we can, even on partial failure.
    // Don't bail mid-asset -- finish the asset's stdout block, report the
    // failure on stderr, and propagate the result code so main() can
    // escalate it.
    BunAssetDataResult dres = {0};
    bun_result_t dr = bun_process_asset_data(ctx, header, r, &dres);
    if (dr != BUN_OK) {
        fprintf(stderr, "violation: asset[%zu]: %s\n", i, dres.error);
        printf("  data_preview      = (unavailable)\n");
        return dr;
    }
    printf("  data_preview      = ");
    if (dres.is_printable) {
        printf("\"%.*s\"\n", (int)dres.preview_len, dres.preview);
    } else {
        for (size_t k = 0; k < dres.preview_len; k++) printf("%02x ", dres.preview[k]);
        printf("\n");
    }
    return BUN_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
        return BUN_ERR_IO;
    }
    const char *path = argv[1];

    BunParseContext ctx    = {0};
    BunHeader       header = {0};

    bun_result_t result = bun_open(path, &ctx);
    if (result != BUN_OK) {
        if (result == BUN_ERR_NOT_FOUND) {
            fprintf(stderr, "Error: file not found: '%s'\n", path);
        } else {
            fprintf(stderr, "Error: could not open '%s'\n", path);
        }
        return result;
    }

    result = bun_parse_header(&ctx, &header);
    if (result != BUN_OK) {
        // Print whatever the parser did manage to learn before failing.
        print_violations(&ctx);
        print_header(&header);
        bun_close(&ctx);
        return result;
    }

    result = bun_parse_assets(&ctx, &header);

    // Validate and populate asset names from the string table.
    // Run even if parse_assets returned non-OK so we report as many
    // violations as possible (brief section 5.2d).
    if (ctx.record_count > 0) {
        bun_result_t str_result =
            bun_validate_string_table(&ctx, &header,
                                      ctx.records, (u32)ctx.record_count);
        if (str_result == BUN_MALFORMED && result == BUN_OK) {
            result = BUN_MALFORMED;
        }
        // Populate display names regardless of validation outcome so that
        // print_asset can show whatever was readable.
        bun_populate_names(&ctx, &header);
    }

    // Brief: print violations on stderr (one per line) and as much of the
    // file as could be safely parsed on stdout. This applies on success
    // and failure -- there may be UNSUPPORTED violations even when the
    // file is otherwise structurally fine.
    print_violations(&ctx);

    print_header(&header);
    for (size_t i = 0; i < ctx.record_count; i++) {
        bun_result_t dr = print_asset(i, &ctx, &header, &ctx.records[i], ctx.names, ctx.record_count);
        // Track worst result. MALFORMED beats UNSUPPORTED beats OK.
        if (dr == BUN_MALFORMED) {
            result = BUN_MALFORMED;
        } else if (dr == BUN_UNSUPPORTED && result == BUN_OK) {
            result = BUN_UNSUPPORTED;
        }
    }

    bun_close(&ctx);
    return result;
}
