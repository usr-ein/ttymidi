CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
SRC      = src/ttymidi.c
BIN      = ttymidi

# Cross-build (Docker): produce a static ARM binary from any host.
ARM_PLATFORM ?= linux/arm64
DIST         ?= dist

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) -lasound

clean:
	rm -f $(BIN)
	rm -rf $(DIST)

install:
	install -m 0755 $(BIN) /usr/local/bin

uninstall:
	rm /usr/local/bin/$(BIN)

format:
	clang-format -i $(SRC)

lint:
	cppcheck --enable=all --std=c11 --inline-suppr --suppress=missingIncludeSystem $(SRC)

# Build a fully static ARM binary inside Docker and drop it in $(DIST)/.
# Runs on any ARM Linux of the target arch (e.g. Raspberry Pi OS).
docker-arm:
	docker buildx build --platform $(ARM_PLATFORM) --target export \
		--output type=local,dest=$(DIST) .
	@echo "==> $(DIST)/$(BIN) ($(ARM_PLATFORM), static)"

.PHONY: all clean install uninstall format lint docker-arm
