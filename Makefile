CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -pedantic
SRC      = $(wildcard src/*.c)
BIN      = ttymidi

# Version baked into the binary at compile time. When HEAD is exactly on a tag
# (e.g. a release built by CI) it reads "<tag> (<short-hash>)", e.g.
# "v0.80 (8bc7456)"; otherwise it's just the short hash, e.g. "8bc7456".
# Falls back to "unknown" outside a git checkout (e.g. a release tarball).
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_TAG  := $(shell git describe --tags --exact-match HEAD 2>/dev/null)
VERSION  ?= $(if $(GIT_HASH),$(if $(GIT_TAG),$(GIT_TAG) ($(GIT_HASH)),$(GIT_HASH)),unknown)
VERSION_FLAG = -DTTYMIDI_VERSION='"$(VERSION)"'

# Tests (pure MIDI logic + serial-write helper + capture replay, no ALSA) -- any host.
TEST_BINS = tests/test_midi tests/test_dumps tests/test_serial_io

# Cross-build (Docker): produce a static ARM binary from any host.
ARM_PLATFORM ?= linux/arm64
DIST         ?= dist

# Deploy (install-remote): ssh alias of the target host.
HOST    ?= trimixxx-pi
# Service restarted after installing, so the new binary actually takes over --
# otherwise the old one keeps running. try-restart is a no-op when the unit is
# not active or not present; set SERVICE= to skip the restart entirely.
SERVICE ?= trimixxx-bridge.service

# End-to-end tests run inside a Lima VM (real ALSA sequencer). Name of that VM.
E2E_VM       ?= ttymidi-e2e

all:
	$(CC) $(CFLAGS) $(VERSION_FLAG) $(SRC) -o $(BIN) -lasound -lpthread

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
	$(CC) $(CFLAGS) -Isrc -Itests tests/test_midi.c       src/midi.c       -o tests/test_midi
	$(CC) $(CFLAGS) -Isrc -Itests tests/test_dumps.c      src/midi.c       -o tests/test_dumps
	$(CC) $(CFLAGS) -Isrc -Itests tests/test_serial_io.c  src/serial_io.c  -o tests/test_serial_io
	./tests/test_midi
	./tests/test_dumps
	./tests/test_serial_io

# Regenerate the end-to-end fixtures from the raw captures in tests/fixtures/.
fixtures:
	python3 tests/gen_dump_fixtures.py

# Build a fully static ARM binary inside Docker and drop it in $(DIST)/.
# Runs on any ARM Linux of the target arch (e.g. Raspberry Pi OS).
docker-arm:
	docker buildx build --platform $(ARM_PLATFORM) --target export \
		--build-arg TTYMIDI_VERSION='$(VERSION)' \
		--output type=local,dest=$(DIST) .
	@echo "==> $(DIST)/$(BIN) ($(ARM_PLATFORM), static)"

# Build the static ARM binary and install it on $(HOST) as /usr/local/bin/$(BIN)
# (mode 0755), then restart $(SERVICE) so the new binary takes over. Staged via
# /tmp because /usr/local/bin needs sudo.
install-remote: docker-arm
	scp $(DIST)/$(BIN) $(HOST):/tmp/$(BIN)
	ssh $(HOST) 'sudo install -m 0755 /tmp/$(BIN) /usr/local/bin/$(BIN) && rm -f /tmp/$(BIN)$(if $(SERVICE), && sudo systemctl try-restart $(SERVICE))'
	@echo "==> $(HOST):/usr/local/bin/$(BIN)"

# End-to-end tests against a REAL ALSA sequencer, provided by a Lima VM (macOS
# and the CI runners both need it -- their host kernels lack snd-seq). Boots the
# VM, runs the whole suite inside it, and stops the VM afterwards -- even if the
# tests fail. Requires Lima: `brew install lima` (macOS) or the Linux release.
test-e2e:
	@command -v limactl >/dev/null 2>&1 || { echo "Lima not installed -- run 'brew install lima' (macOS); see e2e-test/README.md"; exit 1; }
	bash e2e-test/vm/run.sh; status=$$?; limactl stop $(E2E_VM) 2>/dev/null || true; exit $$status

# Delete the e2e VM and reclaim its disk image (next test-e2e re-provisions).
clean-e2e:
	limactl delete -f $(E2E_VM) 2>/dev/null || true

.PHONY: all clean install uninstall install-remote format lint test fixtures docker-arm test-e2e clean-e2e
