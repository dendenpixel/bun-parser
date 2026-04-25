#include <stdio.h>
#include <stdlib.h>

#include "bun.h"

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

static void print_asset(size_t i, const BunAssetRecord *r,
                        char *const *names, size_t name_count) {
    printf("\n=== Asset %zu ===\n", i);

    // Name comes from the string-table module. Until that module exists,
    // names may be NULL or the array itself may be NULL.
    const char *name = "(pending string-table module)";
    if (names && i < name_count && names[i]) {
        name = names[i];
    }
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
        fprintf(stderr, "Error: could not open '%s'\n", path);
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

    // Brief: print violations on stderr (one per line) and as much of the
    // file as could be safely parsed on stdout. This applies on success
    // and failure -- there may be UNSUPPORTED violations even when the
    // file is otherwise structurally fine.
    print_violations(&ctx);

    print_header(&header);
    for (size_t i = 0; i < ctx.record_count; i++) {
        print_asset(i, &ctx.records[i], ctx.names, ctx.record_count);
    }

    bun_close(&ctx);
    return result;
}
