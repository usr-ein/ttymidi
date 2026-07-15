# ttymidi end-to-end tests

Black-box tests that run the real `ttymidi` binary and drive it through its two
external interfaces — a **PTY** standing in for the serial device and the **ALSA
sequencer** standing in for the MIDI host (Mixxx). Nothing in the C code or
binary is modified or specially built.

- `harness.py` — the `Bridge` handle and timeout-bounded read helpers.
- `conftest.py` — the `bridge` fixture: opens a PTY, launches `ttymidi -s <pty>`,
  connects an `rtmidi` client to its two ALSA ports, tears everything down.
- `test_directions.py` — both directions (PTY→ALSA and ALSA→PTY), including
  SysEx and the oversized-SysEx drop-and-recover behaviour.

Managed with [uv](https://docs.astral.sh/uv/). Python is pinned to **3.12** — the
newest CPython with published `python-rtmidi` wheels (cp38–cp312), so it installs
without a source build everywhere. `python-rtmidi` is a C extension, so building
it from source (other Python versions) additionally needs `libasound2-dev`,
`pkg-config`, and a compiler.

## The catch: you need a real ALSA sequencer

The suite needs `/dev/snd/seq` — no sound *hardware*, but the sequencer kernel
module must be present. That rules out running it directly on two common hosts:

- **macOS** has no ALSA at all.
- **GitHub-hosted runners** (and many minimal cloud kernels) ship kernels with
  the sound subsystem compiled out — there is no `snd-seq` module to load.

So there are two ways to run it, depending on your host.

## Running in a VM (macOS, CI, or any host) — recommended

`vm/run.sh` boots a small [Lima](https://lima-vm.io/) VM whose ordinary Ubuntu
`-generic` kernel provides the ALSA sequencer, then builds `ttymidi` and runs the
whole suite inside it. Lima hardware-accelerates via **vz/HVF on macOS** and
**qemu/KVM on Linux**, so the exact same script runs locally and in CI.

Install Lima once:

```sh
brew install lima                    # macOS (also works on Linux with Homebrew)
```

On a Linux host without Homebrew, grab the release binary (what CI does):

```sh
VER=2.1.4
curl -fsSL "https://github.com/lima-vm/lima/releases/download/v${VER}/lima-${VER}-Linux-$(uname -m).tar.gz" \
  | sudo tar -C /usr/local -xzf -
```

Then, from the repo root:

```sh
make test-e2e     # boot the VM, run the whole suite inside it, stop the VM
make clean-e2e    # delete the VM to reclaim its disk
```

or run the harness directly, which leaves the VM running for fast re-runs:

```sh
./e2e-test/vm/run.sh
```

The first run downloads the VM image and provisions it (build tools + uv); later
runs reuse the VM. This is also what the `e2e` GitHub workflow runs on
`ubuntu-latest` (with `/dev/kvm` enabled).

## Running directly (on a Linux host that already has snd-seq)

If your kernel does provide the sequencer (most desktop/server distros do):

```sh
sudo modprobe snd-seq-dummy          # creates /dev/snd/seq
cd e2e-test
uv sync
uv run ruff check .
uv run ruff format --check .
uv run basedpyright
TTYMIDI_BIN=../ttymidi uv run pytest -v   # build ../ttymidi first with `make`
```

`TTYMIDI_BIN` points the harness at a specific binary (defaults to `ttymidi` on
`PATH`); `vm/run.sh` sets it to the binary it builds inside the VM.
