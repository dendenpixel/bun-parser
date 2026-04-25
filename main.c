#include <stdio.h>
#include <stdlib.h>

#include "bun.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_IO;
  }
  const char *path = argv[1];

  BunParseContext ctx = {0};
  BunHeader header  = {0};

  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    fprintf(stderr, "Error: could not open '%s'\n", path);
    return result;
  }

  result = bun_parse_header(&ctx, &header);
  if (result != BUN_OK) {
    // bun_parse_header returns a code; printing the specifics is up to
    // you -- you may want to extend the API to return error details
    fprintf(stderr, "Error: header invalid or unsupported (code %d)\n", result);
    bun_close(&ctx);
    return result;
  }

  result = bun_parse_assets(&ctx, &header);
// print violations (if any) to stderr
    for (size_t i = 0; i < ctx.n_violations; i++) {
        fprintf(stderr, "violation: %s\n", ctx.violations[i]);
    }

    // print what we did parse to stdout — even on failure, per the brief
    printf("BUN header:\n");
    printf("  magic               = 0x%08x\n", header.magic);
    printf("  version             = %u.%u\n", header.version_major, header.version_minor);
    printf("  asset_count         = %u\n", header.asset_count);
    printf("  asset_table_offset  = %llu\n", (unsigned long long)header.asset_table_offset);
    // ... rest of header fields

    for (size_t i = 0; i < ctx.record_count; i++) {
        const BunAssetRecord *r = &ctx.records[i];
        printf("\nasset %zu:\n", i);
        printf("  name        = %s\n", ctx.names[i] ? ctx.names[i] : "(invalid)");
        printf("  data_size   = %llu\n", (unsigned long long)r->data_size);
        printf("  compression = %u\n", r->compression);
        // ... etc
    }
  bun_close(&ctx);
  return result;
}
