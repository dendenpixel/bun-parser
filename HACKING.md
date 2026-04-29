# Contributor's guide

Technical notes on this codebase.

## Repository layout

```
bun.h                  Shared types, on-disk structs, result codes, public API
bun_parse.c            File open/close; header and asset-table parsing
bun_string_table.c/h   String-table validation and name population
bun_data.c/h           Asset data reading and RLE decompression 
main.c                 CLI entry point, formats and prints output
test_bun.c             libcheck test suite (move to tests/ before running make test)
bunfile_generator.py   Python helper for generating test fixtures
Makefile               Build rules
```

## Building

```
make 
make test
make samples-test

```

Enable sanitizers during development by uncommenting the relevant line in the Makefile:

```makefile
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
```

## Running tests

The Makefile expects the test source at `tests/test_bun.c`. Before running `make test`:
1. Run from the project root: `make`

## Architecture

### Parse context

`BunParseContext` (defined in `bun.h`) is the shared state that flows through every parse stage:

- `file` / `file_size` — opened file handle and its size
- `records` / `record_count` — asset records populated by `bun_parse_assets`
- `names` — display names populated by `bun_populate_names` (parallel to `records`)
- `violations` / `n_violations` — growing list of spec violations, allocated on the heap

No parsing function prints anything. They only append to `ctx->violations` and return a result code. `main.c` is the only place output is produced.

**Note:** `bun_data.c` uses a different pattern, it writes errors into a `BunAssetDataResult.error` field rather than `ctx->violations`. It is not yet wired into the Makefile `LIB` or called from `main.c`. When integrating it, align its error reporting with the `add_violationf` / `ctx->violations` pattern used everywhere else.

### Result codes

| Code | Value | Meaning |
|------|-------|---------|
| `BUN_OK` | 0 | Valid |
| `BUN_MALFORMED` | 1 | Structural spec violation |
| `BUN_UNSUPPORTED` | 2 | Feature not implemented |
| `BUN_ERR_IO` | 3 | I/O error or file not found |
| `BUN_ERR_NOT_FOUND` | 4 | fopen failed with errno == ENOENT |

Use the `escalate()` helper (in `bun_parse.c`) to promote a running worst-result: `MALFORMED` beats `UNSUPPORTED` beats `OK`. Never downgrade a result once escalated.

### Adding a new validation check

1. Write a `static void check_*(BunParseContext *ctx, ..., bun_result_t *status)` function.
2. Call `add_violation` or `add_violationf` to record the human-readable message.
3. Call `escalate(status, BUN_MALFORMED)` or `escalate(status, BUN_UNSUPPORTED)` as appropriate.
4. Call your function from `bun_parse_assets` (or the relevant parse stage) and escalate its status into the loop's `worst`.

### Little-endian decoding

Never `memcpy` disk bytes directly into a struct — padding and alignment differ between the on-disk layout and the C struct. Use the `read_u16_le`, `read_u32_le`, and `read_u64_le` helpers in `bun_parse.c` to decode each field from a raw byte buffer.

### Integer overflow discipline

All offset arithmetic that could overflow must be guarded explicitly:

- Promote `u32` operands to `u64` before addition.
- Check `a > UINT64_MAX - b` before computing `a + b` on `u64` values.
- For `a * b` on `u64`, check `(a * b) / a == b` after the multiplication (used in the asset table sizing in `bun_parse_header` and `bun_parse_assets`). 
- Use `safe_seek` (wraps `fseeko`) when seeking to a `u64` offset; it rejects values that exceed `INT64_MAX`.

### Violation messages

- Use `add_violationf` for any message that interpolates a value (asset index, offset, etc.).
- Use `add_violation` for fixed strings.
- Prefer `asset[%u]: ...` as the message prefix for per-asset violations.
- Collect all violations before returning — do not short-circuit on the first error unless the error makes further parsing unsafe (e.g. `BUN_ERR_IO`).

## Coding conventions

- C11 (`-std=c11`), compiled with `-Wall -Wextra -Wpedantic`.
- Integer types: use `u8`, `u16`, `u32`, `u64` (typedef'd in `bun.h`) for on-disk values. Use `size_t` for in-memory counts and lengths.
- No global state. All state lives in `BunParseContext`.
- Parsing functions must not call `printf`/`fprintf`. Output is `main.c`'s responsibility.
- `bun_close` must be safe to call on a partially-initialised context (NULL members are fine).
