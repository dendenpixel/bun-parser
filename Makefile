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

.PHONY: all test samples-test fixtures clean

all: bun_parser

bun_parser: $(MAIN) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

fixtures:
	@mkdir -p tests/fixtures/valid tests/fixtures/invalid
	@echo "=== Generating test fixtures ==="
	python3 bunfile_generator.py --all

# The test binary links the same source files, but not main.c (which has its
# own main()). libcheck provides the test runner's main() instead.
test: tests/test_runner fixtures
	./tests/test_runner

tests/test_runner: $(TEST) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $$(pkg-config --cflags --libs check)

# End-to-end test: runs the compiled bun_parser against every fixture file
# and checks the exit code matches the expected result code per spec.
samples-test: bun_parser fixtures

	@echo "=== VALID samples (expect exit 0) ==="
	@for f in tests/fixtures/valid/*.bun; do                              \
	    ./bun_parser "$$f" >/dev/null 2>&1;                               \
	    rc=$$?;                                                           \
	    if [ "$$rc" -eq 0 ]; then                                         \
	        echo "  PASS  $$(basename $$f) -> $$rc";                      \
	    else                                                              \
	        echo "  FAIL  $$(basename $$f) -> $$rc (expected 0)";         \
	        exit 1;                                                       \
	    fi;                                                               \
	done
	@echo "=== INVALID samples (expect exit 1 or 2) ==="
	@for f in tests/fixtures/invalid/*.bun; do                            \
	    ./bun_parser "$$f" >/dev/null 2>&1;                               \
	    rc=$$?;                                                           \
	    if [ "$$rc" -eq 1 ] || [ "$$rc" -eq 2 ]; then                     \
	        echo "  PASS  $$(basename $$f) -> $$rc";                      \
	    else                                                              \
	        echo "  FAIL  $$(basename $$f) -> $$rc (expected 1 or 2)";    \
	        exit 1;                                                       \
	    fi;                                                               \
	done
	@echo "=== TOTAL: $$(ls tests/fixtures/valid/*.bun tests/fixtures/invalid/*.bun | wc -l | tr -d ' ') pass / 0 fail ==="

clean:
	-rm -f bun_parser tests/test_runner *.o
	-rm -rf tests/fixtures/valid tests/fixtures/invalid
	
