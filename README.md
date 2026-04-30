# BUN Parser

A command-line tool written in C11 that parses and validates `.bun` binary
asset bundle files against the BUN 1.0 specification, for the 
Secure Coding project.

## Group Members

| Name                 | Student ID | GitHub Username     |
|----------------------|------------|---------------------|
| Dennis Quek          | -   | dendenpixel         |
| Teammate C | -   | teammate-c        |
| Teammate A         | -   | teammate-a             |
| Teammate B       | -   | teammate-b   |

## Building

Requires `gcc`, GNU `make`, `check`, and `pkg-config`. On Ubuntu / WSL:

```sh
sudo apt install build-essential check pkg-config python3
```

Then, from the project root:

```sh
make              # builds ./bun_parser
make test         # builds and runs the test suite
make clean        # removes build artefacts
```

### Sanitizer build

Uncomment this line in the `Makefile`:

```makefile
# CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
```

Then `make clean && make test`. Re-comment before submission.

## Usage

```sh
./bun_parser <file.bun>
```

### Exit codes

| Code | Symbol              | Meaning                                      |
|------|---------------------|----------------------------------------------|
| 0    | `BUN_OK`            | File is fully valid                          |
| 1    | `BUN_MALFORMED`     | File violates the spec                       |
| 2    | `BUN_UNSUPPORTED`   | File uses an unsupported feature             |
| 3    | `BUN_ERR_IO`        | Generic I/O error                            |
| 4    | `BUN_ERR_NOT_FOUND` | Path does not exist                          |

## Source layout

| File                        | Purpose |
|-----------------------------|---------|
| `bun.h`                     | Shared types, structs, result codes, spec constants |
| `bun_parse.c`               | Header parsing, asset table parsing, bounds and overlap checks |
| `bun_string_table.c` / `.h` | String table validation, streaming name read |
| `bun_data.c` / `.h`         | Per-asset data read, RLE decompression, preview extraction |
| `main.c`                    | CLI entry point, output formatting |
| `tests/test_bun.c`          | libcheck test suite |
| `tests/fixtures/`           | `.bun` fixture files used by tests |
| `bunfile_generator.py`      | Python helper for generating `.bun` files |
| `Makefile`                  | Build rules: `all`, `test`, `clean`, `fixtures` |
| `HACKING.md`                | Contributor and architecture notes |

## Testing

| TCase                  | Tests | What it covers |
|------------------------|-------|----------------|
| `header-tests`         | 12    | Magic/version validation, file-too-short, misaligned offset/size, zero fields, reserved field, section-overlaps-header, combo bad magic+version |
| `string-table-tests`   | 9     | Valid name, zero-length name, non-printable byte, name exceeds table, integer overflow, adjacent names, nonzero table offset, prefix read full, prefix read truncated |
| `open-error-tests`     | 1     | `BUN_ERR_NOT_FOUND` on a missing path |
| `header-bounds-tests`  | 4     | Spec 9.2 (past-EOF), spec 9.3 (overlap, adjacency, overflow safety) |
| `dos-cap-tests`        | 2     | `BUN_MAX_RECORDS` rejects huge counts before `calloc` |
| `data-preview-tests`   | 2     | End-to-end pipeline with printable + binary payloads |
| `data-section-tests`   | 9     | RLE valid, zlib unsupported, nonzero checksum, unknown flags, RLE odd size, RLE zero count, RLE size mismatch, uncompressed bad size, large file (650 MB, skipped if absent) |
| **Total**              | **39** | |

Most tests use `fmemopen(3)` to construct in-memory BUN files — no on-disk
fixtures required. The three fixture-based tests use files committed under
`tests/fixtures/`.