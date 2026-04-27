# BUN Parser

A command-line tool written in C11 that parses and validates `.bun` binary
asset bundle files against the BUN 1.0 specification, for the 
Secure Coding project.

## Overview

BUN files pack multiple named assets into a single binary container. The
parser reads the file header, the asset entry table, the string table,
and (for each asset) the data section. It validates every field against
the spec and collects all violations before exiting.

- **Violations** are printed to `stderr`, one per line, prefixed with
  `violation:`.
- **Parsed contents** are printed to `stdout` in a human-readable form.
- The parser **always prints as much as it could safely decode**, even on
  failure (per spec section 5.2d).

## Group Members

| Name                 | Student ID | GitHub Username     |
|----------------------|------------|---------------------|
| Dennis Quek          | -   | dendenpixel         |
| Teammate C | -   | teammate-c        |
| Teammate A         | -   | teammate-a             |
| Teammate B       | -   | teammate-b   |
| Former member          | -   | former-member         |

## Building

Requires `gcc`, GNU `make`, `check` (for the test suite), and
`pkg-config`. On Ubuntu / WSL:

```sh
sudo apt install build-essential check pkg-config python3
```

Then, from the project root:

```sh
make              # builds ./bun_parser
make test         # builds and runs tests/test_runner (21 tests)
make clean        # removes build artefacts
```

`make test` works out of the box — the required fixture files are
committed to `tests/fixtures/`.

### Sanitizer build

For development and verification, uncomment this line in the `Makefile`:

```makefile
# CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
```

Then `make clean && make test`. AddressSanitizer and UndefinedBehaviorSanitizer
will catch out-of-bounds reads, leaks, and signed-overflow UB. **Re-comment
the line before submission** — sanitizers slow the binary 10-100× and the
brief explicitly says not to ship with them on.

### Regenerating test fixtures

The fixtures are committed, but if `bunfile_generator.py` changes you can
regenerate them with:

```sh
make fixtures
```

This rebuilds `tests/fixtures/valid/01-empty.bun` and the two corrupted
variants under `tests/fixtures/invalid/`.

## Usage

```sh
./bun_parser <file.bun>
```

### Exit codes

| Code | Symbol              | Meaning                                                                                          |
|------|---------------------|--------------------------------------------------------------------------------------------------|
| 0    | `BUN_OK`            | File is fully valid                                                                              |
| 1    | `BUN_MALFORMED`     | File violates the spec (structural error)                                                        |
| 2    | `BUN_UNSUPPORTED`   | File uses a feature this parser does not implement (zlib compression, non-zero CRC checksum, asset count above the DoS cap, etc.) |
| 3    | `BUN_ERR_IO`        | Generic I/O error (read/seek failed)                                                             |
| 4    | `BUN_ERR_NOT_FOUND` | The given path does not exist (`errno == ENOENT` at `fopen`); distinct from other I/O failures   |

When multiple validation results would apply, the parser escalates to the
worst (`MALFORMED` beats `UNSUPPORTED` beats `OK`). I/O and not-found
errors are terminal and short-circuit immediately.

### Example

```sh
$ ./bun_parser minimal.bun
=== BUN Header ===
magic                = 0x304E5542
version              = 1.0
asset_count          = 1
...

=== Asset 0 ===
  name              = hello
  data_preview      = "Hello, BUN world!"
$ echo $?
0

$ ./bun_parser /tmp/no-such-file.bun
Error: file not found: '/tmp/no-such-file.bun'
$ echo $?
4
```

## BUN File Format (summary)

All multi-byte integers are little-endian. The full specification is in
`the format spec`.

### Header (60 bytes, spec section 4)

| Field                 | Type | Description                              |
|-----------------------|------|------------------------------------------|
| `magic`               | u32  | Must be `0x304E5542` ("BUN0")            |
| `version_major`       | u16  | Must be 1                                |
| `version_minor`       | u16  | Must be 0                                |
| `asset_count`         | u32  | Number of asset records                  |
| `asset_table_offset`  | u64  | Byte offset of the asset table           |
| `string_table_offset` | u64  | Byte offset of the string table          |
| `string_table_size`   | u64  | Byte size of the string table            |
| `data_section_offset` | u64  | Byte offset of the data section          |
| `data_section_size`   | u64  | Byte size of the data section            |
| `reserved`            | u64  | Ignored                                  |

All offsets and sizes must be divisible by 4.

### Asset record (48 bytes each, spec section 5)

Each record describes one asset: name location in the string table
(`name_offset`, `name_length`), data location in the data section
(`data_offset`, `data_size`), `uncompressed_size`, `compression`, `type`,
`checksum`, and `flags`.

### String table (spec section 6)

Asset names are stored as contiguous bytes, **not null-terminated**. Each
name must consist entirely of printable ASCII (`0x20`–`0x7E`).

## Validation rules enforced

The parser implements **every** rule in the BUN 1.0 spec, plus a few
defensive extensions documented below.

**Header (sections 4 + 9):**
- Magic must be exactly `BUN0`
- Version must be exactly 1.0 (other versions → `UNSUPPORTED`)
- All offsets and sizes 4-byte aligned (section 4.1)
- Every section fits inside the file (section 9.2)
- No two sections overlap (section 9.3)
- Overflow-safe: u64 offset+size arithmetic never wraps

**Asset records (sections 5 + 9.4):**
- Every record's data range fits inside the data section
- `name_offset + name_length` does not overflow u32
- `data_offset + data_size` does not overflow u64
- Compression: 0 (none) and 1 (RLE) are accepted; 2 (zlib) and other
  values are `UNSUPPORTED`
- For uncompressed assets, `uncompressed_size` must be 0
- For RLE-compressed assets, `data_size` must be even (pairs of bytes)
- Non-zero CRC-32 checksums are `UNSUPPORTED` (verification not
  implemented)
- Only flag bits `ENCRYPTED` (`0x1`) and `EXECUTABLE` (`0x2`) are
  recognised; unknown bits → `UNSUPPORTED`

**String table (section 6):**
- Names are non-empty
- Name range fits inside the string table (no overflow)
- Every name byte is printable ASCII (`0x20`–`0x7E`)
- Validated by streaming in 256-byte chunks (no full-table buffer)

**Data section:**
- Each asset's data range fits inside the data section
- RLE pairs with `count == 0` are rejected
- RLE decompressed length must equal `uncompressed_size` exactly (early
  exit if it would exceed — guards against decompression bombs)

## Custom error codes and design decisions

The brief permits additional result codes in the range 3–10, provided
they are documented here.

### `BUN_ERR_NOT_FOUND` (code 4)

Returned by `bun_open` when `fopen` fails with `errno == ENOENT`. Lets
callers distinguish "user typo'd the path" from a real I/O failure (disk
error, permission denied, etc.). All other `fopen` failures still return
`BUN_ERR_IO`.

### `BUN_MAX_RECORDS = 1,000,000` (DoS protection)

A hostile file could declare `asset_count = UINT32_MAX`, which would
cause `calloc(4.3 × 10⁹, 48)` to request ~206 GB of memory. Without
overcommit, this fails and we return `BUN_ERR_IO`, masquerading the real
cause; with overcommit, the allocation succeeds and the parser crashes on
first access.

We cap `asset_count` at 1,000,000 (~48 MB of in-memory record state) and
return `BUN_UNSUPPORTED` for anything above. This is `UNSUPPORTED` rather
than `MALFORMED` because the file may be structurally valid; we are
choosing not to load it.

### Partial-output policy

Per spec 5.2d, the parser prints as much as it can safely decode even
when it returns non-zero. If a per-asset stage fails (e.g.
`bun_process_asset_data` cannot read an asset's data), the asset's stdout
block is still completed (`data_preview = (unavailable)`) and parsing
continues to the next asset. This guarantees a reviewer can see exactly
how far the parser got.

## Memory usage and large files

The parser **does not load files into memory**. From the spec section 5.3,
"marks will be awarded to parsers that handle large files with sub-linear
memory usage" — this implementation satisfies that:

| Stage              | Buffer size on stack | Heap usage           |
|--------------------|----------------------|----------------------|
| Header parse       | 60 bytes             | none                 |
| Asset table parse  | 48 bytes (per record, reused) | 48 × N (N = asset count, ≤ 48 MB at the cap) |
| String validation  | 256 bytes            | up to 61 × N for display names  |
| Data section read  | 4096 bytes           | 60 bytes per asset (preview only) |

A 638 MB sample file is processed without ever holding more than a few KB
of file content in memory at once, regardless of how large any individual
asset is. RLE decompression streams pair-by-pair into the 60-byte preview
buffer rather than materialising the full decompressed output.

## Source layout

| File                       | Purpose |
|----------------------------|---------|
| `bun.h`                    | Shared types (`u8`–`u64`, structs, result codes), spec constants |
| `bun_parse.c`              | File open/close, header parsing, asset entry table parsing, all section bounds and overlap checks |
| `bun_string_table.c` / `.h`| String table validation, streaming name read, display-name population |
| `bun_data.c` / `.h`        | Per-asset data read, RLE decompression, preview extraction |
| `main.c`                   | CLI entry point; orchestrates the parse stages and prints output |
| `tests/test_bun.c`         | libcheck test suite — 21 tests across header, string-table, bounds, DoS, and data-preview |
| `tests/fixtures/`          | Tiny `.bun` files used by the fixture-based tests |
| `bunfile_generator.py`     | Python helper for generating valid `.bun` files |
| `the format spec`             | The BUN 1.0 specification |
| `Makefile`                 | Build rules: `all`, `test`, `clean`, `fixtures` |
| `HACKING.md`               | Contributor / architecture notes |

## Testing

The test suite uses [libcheck](https://libcheck.github.io/check/) and
covers six areas:

| TCase                  | Tests | What it covers |
|------------------------|-------|----------------|
| `header-tests`         | 3     | Fixture-based: valid file, bad magic, bad version |
| `string-table-tests`   | 9     | All 3 spec rules + integer overflow + boundary cases |
| `open-error-tests`     | 1     | `BUN_ERR_NOT_FOUND` on a missing path |
| `header-bounds-tests`  | 4     | Spec 9.2 (past-EOF), spec 9.3 (overlap, adjacency, overflow safety) |
| `dos-cap-tests`        | 2     | `BUN_MAX_RECORDS` rejects huge counts before `calloc` |
| `data-preview-tests`   | 2     | End-to-end pipeline with printable + binary payloads |

Most tests use `fmemopen(3)` to construct in-memory BUN files via
`build_header` and `build_record` helpers — no on-disk fixtures required.
The three fixture-based tests use the small files committed under
`tests/fixtures/`.

## External links (group internal)

- [Project setup notes (Google Doc)](https://docs.google.com/document/d/17DBO_G56AeHQVMXINHpcFUW8dg9kY16g_AWHqCXOgBo/edit?tab=t.0#heading=h.fo384rsxtcbe)
- [Task allocation (Google Sheet)](https://docs.google.com/spreadsheets/d/1X8TocJiEzL2VtqQRKWug4Q9oBugdCclkPkHXbsui2X8/edit?gid=0#gid=0)
