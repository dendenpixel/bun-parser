# Contributor's guide

## Project structure
- `bun_parse.c` — header + asset parsing, validation logic
- `bun_string_table.c` — string table validation and name extraction
- `bun_data.c` — RLE decompression and data preview
- `main.c` — output formatting and entry point
- `bun.h` — shared types, structs, constants (don't edit)
- `tests/test_bun.c` — unit tests (libcheck)

## Building with sanitizers
Uncomment the CFLAGS line in the Makefile:

    CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g

Then `make clean && make all`. Do NOT ship a release build with these on.

## Running tests
    make test          # requires libcheck: sudo apt-get install check
    make samples-test  # end-to-end exit code checks against fixture files

## Coding standards
- C11, GCC, -Wall -Wextra -Wpedantic — no warnings tolerated
- All multi-byte integers read with explicit little-endian helpers (read_u16/32/64_le)
  — never memcpy into structs (padding is not guaranteed to match on-disk layout)
- All u64 arithmetic involving offsets/sizes checked for overflow before use
- Parsing functions never print — violations collected in ctx, printed by main.c
- Violation messages go to stderr; content output goes to stdout

## Adding test cases
Use `bunfile_generator.py` to craft new `.bun` files — see the existing cases at the
bottom of that file for patterns. Add the file under tests/fixtures/valid or invalid,
and add a corresponding unit test or samples-test entry.
