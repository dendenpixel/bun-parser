# BUN Parser

A command-line tool written in C that parses and validates `.bun` binary asset bundle files against the BUN 1.0 specification for Project. 

## Overview

BUN files pack multiple named assets into a single binary container. The parser reads the file header, asset table, and string table, validates each field against the spec and reports all violations found before exiting.

Violations are printed to `stderr`, one per line. A human readable summary of every successfully-parsed header field and asset record is printed to `stdout`.

# Group Member
| Name | Student ID | GitHub Username |
|------|------------|-----------------|
|Dennis Quek|-|dendenpixel|
|Teammate C|-|teammate-c|
|Teammate A|-|teammate-a|
|Teammate B|-|teammate-b|
|Former member|-|former-member|

## Building

Requires GCC and GNU Make.

```
make
```

This produces a `bun_parser` executable in the project root.

To run the test suite (requires [libcheck](https://libcheck.github.io/check/)):

```
make test
```

## Usage

```
./bun_parser <file.bun>
```

**Exit codes**

| Code | Meaning |
|------|---------|
| 0    | `BUN_OK` — file is fully valid |
| 1    | `BUN_MALFORMED` — file violates the spec (structural error) |
| 2    | `BUN_UNSUPPORTED` — file uses a feature not implemented (e.g. zlib compression, non-zero checksum) |
| 3    | `BUN_ERR_IO` — file could not be opened or read |

The parser collects all violations before exiting and always prints as much of the file as it can safely decode, even on failure.

## BUN File Format (summary)

All multi-byte integers are little-endian.

**Header (60 bytes)**

| Field | Size | Description |
|-------|------|-------------|
| magic | u32 | Must be `0x304E5542` ("BUN0") |
| version_major | u16 | Must be 1 |
| version_minor | u16 | Must be 0 |
| asset_count | u32 | Number of asset records |
| asset_table_offset | u64 | Byte offset of the asset table |
| string_table_offset | u64 | Byte offset of the string table |
| string_table_size | u64 | Byte size of the string table |
| data_section_offset | u64 | Byte offset of the data section |
| data_section_size | u64 | Byte size of the data section |
| reserved | u64 | Ignored |

All offsets and sizes must be divisible by 4.

**Asset record (48 bytes each)**

Each record describes one asset: its name location in the string table, its data location in the data section, compression type, type tag, checksum, and flags.

**String table**

Asset names are stored as contiguous bytes, not null-terminated. Each name must consist entirely of printable ASCII (0x20–0x7E).

## Validation rules enforced

- Magic must match `BUN0`; version must be exactly 1.0
- All offsets and sizes must be 4-byte aligned
- Asset record name ranges must lie within the string table
- Asset data ranges must lie within the data section, with no integer overflow
- Compression: 0 (none) and 1 (RLE) are supported; 2 (zlib) and others are `UNSUPPORTED`
- For uncompressed assets, `uncompressed_size` must be 0
- RLE `data_size` must be even (pairs of bytes)
- Non-zero checksums are `UNSUPPORTED` (CRC-32 not implemented)
- Unknown flag bits are `UNSUPPORTED`; known flags are `ENCRYPTED` (0x1) and `EXECUTABLE` (0x2)
- Asset names must contain only printable ASCII

## Source layout

| File | Purpose |
|------|---------|
| `bun.h` | Shared types, on-disk structs, result codes |
| `bun_parse.c` | File open/close, header parsing, asset table parsing |
| `bun_string_table.c/h` | String table validation and name population |
| `bun_data.c/h` | Asset data reading and RLE decompression |
| `main.c` | CLI entry point; prints parsed output and violations |
| `test_bun.c` | Test suite (libcheck); place under `tests/` before running `make test` |
| `bunfile_generator.py` | Python helper for generating test `.bun` files |
| `Makefile` | Build rules for `bun_parser` and test runner |

