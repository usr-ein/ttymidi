CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
SRC      = $(wildcard src/*.c)
BIN      = ttymidi

# Tests (pure MIDI logic + end-to-end capture replay, no ALSA) -- any host.
TEST_BINS = tests/test_midi tests/test_dumps

# Cross-build (Docker): produce a static ARM binary from any host.
ARM_PLATFORM ?= linux/arm64
DIST         ?= dist

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) -lasound

clean:
	rm -f $(BIN) $(TEST_BINS)
	rm -rf $(DIST)

install:
	install -m 0755 $(BIN) /usr/local/bin

uninstall:
	rm /usr/local/bin/$(BIN)

format:
	clang-format -i $(wildcard src/*.c) $(wildcard src/*.h) $(wildcard tests/*.c)

lint:
	cppcheck --enable=all --std=c11 --inline-suppr --suppress=missingIncludeSystem $(SRC)

# Build and run the tests. No ALSA needed, so this runs on any host.
test:
	$(CC) $(CFLAGS) -Isrc -Itests tests/test_midi.c  src/midi.c -o tests/test_midi
	$(CC) $(CFLAGS) -Isrc -Itests tests/test_dumps.c src/midi.c -o tests/test_dumps
	./tests/test_midi
	./tests/test_dumps

# Regenerate the end-to-end fixtures from the raw captures in tests/fixtures/.
fixtures:
	python3 tests/gen_dump_fixtures.py

# Build a fully static ARM binary inside Docker and drop it in $(DIST)/.
# Runs on any ARM Linux of the target arch (e.g. Raspberry Pi OS).
docker-arm:
	docker buildx build --platform $(ARM_PLATFORM) --target export \
		--output type=local,dest=$(DIST) .
	@echo "==> $(DIST)/$(BIN) ($(ARM_PLATFORM), static)"

.PHONY: all clean install uninstall format lint test fixtures docker-arm
