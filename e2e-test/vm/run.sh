#!/usr/bin/env bash
# Run the ttymidi e2e suite inside a Lima VM that provides a real ALSA sequencer,
# for hosts whose own kernel lacks it (macOS, GitHub-hosted runners). Lima uses
# vz/HVF on macOS and qemu/KVM on Linux -- it auto-selects; nothing here is
# platform-specific. Exits non-zero if the suite fails.
set -euo pipefail

VM=ttymidi-e2e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

# 1. Ensure the VM exists and is running. Create + provision it from lima.yaml
#    the first time; just (re)start the existing instance on later runs.
status="$(limactl list --format '{{.Status}}' "$VM" 2>/dev/null || true)"
if [ -z "$status" ]; then
  echo "==> creating Lima VM '$VM' (downloads the image + provisions; one-time)"
  limactl --log-level warn start --name="$VM" --tty=false "$HERE/lima.yaml"
elif [ "$status" != "Running" ]; then
  echo "==> starting existing Lima VM '$VM'"
  limactl --log-level warn start --tty=false "$VM"
fi

# 2. Copy the working tree in (tracked files + local edits). COPYFILE_DISABLE
#    drops macOS AppleDouble "._" files; the guest extract ignores the leftover
#    xattr headers (--warning below), so this stays portable to GNU tar in CI.
#    The excludes drop heavy/host-specific dirs.
TARBALL="$(mktemp -t ttymidi-e2e-XXXXXX).tar"
trap 'rm -f "$TARBALL"' EXIT
COPYFILE_DISABLE=1 tar -cf "$TARBALL" -C "$REPO_ROOT" \
  --exclude=.git --exclude=e2e-test/.venv --exclude=dist \
  --exclude='__pycache__' --exclude=.pytest_cache --exclude=.ruff_cache .
limactl --log-level warn copy "$TARBALL" "$VM:/tmp/repo.tar"

# 3. Build ttymidi and run the full suite inside the VM against the real ALSA
#    sequencer. modprobe/chmod are re-asserted each run so a reused VM still works.
limactl --log-level warn shell "$VM" -- bash -euo pipefail -c '
  export PATH="$HOME/.local/bin:$PATH"
  rm -rf ~/build && mkdir ~/build && tar --warning=no-unknown-keyword -xf /tmp/repo.tar -C ~/build
  cd ~/build
  make clean >/dev/null 2>&1 || true
  make
  sudo modprobe snd-seq-dummy
  sudo chmod -R a+rw /dev/snd
  cd e2e-test
  uv sync --frozen
  uv run ruff check .
  uv run ruff format --check .
  uv run basedpyright
  TTYMIDI_BIN="$HOME/build/ttymidi" uv run pytest -v
'
