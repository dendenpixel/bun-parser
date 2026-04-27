CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

# Flags passed to the linker. You will likely need to define this if you use
# additional libraries.
LDFLAGS =

# Sanitizer flags - uncomment during development and testing.
# Do not leave them enabled in a "release" build as they affect performance.
#
# CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g

LIB     = bun_parse.c bun_string_table.c bun_data.c
MAIN    = main.c
TEST    = tests/test_bun.c

.PHONY: all test clean fixtures

all: bun_parser

bun_parser: $(MAIN) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# The test binary links the same source files, but not main.c (which has its
# own main()). libcheck provides the test runner's main() instead.
test: tests/test_runner
	./tests/test_runner

tests/test_runner: $(TEST) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $$(pkg-config --cflags --libs check)

# Regenerate the test fixtures used by the header-tests TCase. The fixtures
# are committed to the repo so 'make test' works out of the box; this target
# only needs to be run if you change bunfile_generator.py or want to verify
# the fixtures are reproducible. Overwrites any existing files in
# tests/fixtures/{valid,invalid}/.
fixtures:
	mkdir -p tests/fixtures/valid tests/fixtures/invalid
	python3 bunfile_generator.py
	mv minimal.bun tests/fixtures/valid/01-empty.bun
	cp tests/fixtures/valid/01-empty.bun tests/fixtures/invalid/01-bad-magic.bun
	printf '\xDE\xAD\xBE\xEF' | dd of=tests/fixtures/invalid/01-bad-magic.bun \
	    bs=1 count=4 conv=notrunc 2>/dev/null
	cp tests/fixtures/valid/01-empty.bun tests/fixtures/invalid/02-bad-version.bun
	printf '\x99\x00' | dd of=tests/fixtures/invalid/02-bad-version.bun \
	    bs=1 seek=4 count=2 conv=notrunc 2>/dev/null
	@echo "Regenerated fixtures in tests/fixtures/{valid,invalid}/"

clean:
	rm -f bun_parser tests/test_runner *.o
