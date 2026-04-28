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

.PHONY: all test samples-test clean

all: bun_parser

bun_parser: $(MAIN) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# The test binary links the same source files, but not main.c (which has its
# own main()). libcheck provides the test runner's main() instead.
test: tests/test_runner
	./tests/test_runner

tests/test_runner: $(TEST) $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $$(pkg-config --cflags --libs check)

# End-to-end CLI sweep over the provided sample files in
# tests/fixtures/{valid,invalid}/. Every valid sample must exit 0;
# every invalid sample must exit 1 (MALFORMED) or 2 (UNSUPPORTED).
# Returns non-zero if any file produces an unexpected exit code.
samples-test: bun_parser
	@mkdir -p tests/fixtures/valid tests/fixtures/invalid
	@echo "=== VALID samples (expect exit 0) ==="; \
	pass=0; fail=0; \
	for f in tests/fixtures/valid/*.bun; do \
	    [ -e "$$f" ] || continue; \
	    ./bun_parser "$$f" > /dev/null 2>&1; \
	    code=$$?; \
	    if [ $$code -eq 0 ]; then \
	        printf "  PASS  %s -> %d\n" "$$(basename $$f)" "$$code"; \
	        pass=$$((pass+1)); \
	    else \
	        printf "  FAIL  %s -> %d (expected 0)\n" "$$(basename $$f)" "$$code"; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo ""; \
	echo "=== INVALID samples (expect exit 1 or 2) ==="; \
	for f in tests/fixtures/invalid/*.bun; do \
	    [ -e "$$f" ] || continue; \
	    ./bun_parser "$$f" > /dev/null 2>&1; \
	    code=$$?; \
	    if [ $$code -eq 1 ] || [ $$code -eq 2 ]; then \
	        printf "  PASS  %s -> %d\n" "$$(basename $$f)" "$$code"; \
	        pass=$$((pass+1)); \
	    else \
	        printf "  FAIL  %s -> %d (expected 1 or 2)\n" "$$(basename $$f)" "$$code"; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo ""; \
	echo "=== TOTAL: $$pass pass / $$fail fail ==="; \
	[ $$fail -eq 0 ]

clean:
	rm -f bun_parser tests/test_runner *.o
