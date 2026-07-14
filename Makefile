CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
SRC      = $(wildcard src/*.c)
BIN      = ttymidi

# Unit tests (pure MIDI logic, no ALSA) -- run natively on any host.
TEST_SRC = tests/test_midi.c src/midi.c
TEST_BIN = tests/test_midi

# Cross-build (Docker): produce a static ARM binary from any host.
ARM_PLATFORM ?= linux/arm64
DIST         ?= dist

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) -lasound

clean:
	rm -f $(BIN) $(TEST_BIN)
	rm -rf $(DIST)

install:
	install -m 0755 $(BIN) /usr/local/bin

uninstall:
	rm /usr/local/bin/$(BIN)

format:
	clang-format -i $(wildcard src/*.c) $(wildcard src/*.h) tests/test_midi.c

lint:
	cppcheck --enable=all --std=c11 --inline-suppr --suppress=missingIncludeSystem $(SRC)

# Build and run the unit tests. No ALSA needed, so this runs on any host.
test:
	$(CC) $(CFLAGS) -Isrc -Itests $(TEST_SRC) -o $(TEST_BIN)
	./$(TEST_BIN)

# Build a fully static ARM binary inside Docker and drop it in $(DIST)/.
# Runs on any ARM Linux of the target arch (e.g. Raspberry Pi OS).
docker-arm:
	docker buildx build --platform $(ARM_PLATFORM) --target export \
		--output type=local,dest=$(DIST) .
	@echo "==> $(DIST)/$(BIN) ($(ARM_PLATFORM), static)"

.PHONY: all clean install uninstall format lint test docker-arm
