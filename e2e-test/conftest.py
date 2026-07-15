"""pytest fixture that boots a ttymidi process wired to a PTY on the serial side
and the ALSA sequencer on the MIDI side, so tests can drive both directions as a
black box -- no changes to the C code or binary."""

from __future__ import annotations

import os
import subprocess
import termios
import time
import tty
from collections.abc import Iterator

import pytest
import rtmidi

from harness import Bridge, open_named_port

# Absolute path to the freshly built ttymidi binary (CI sets this to ./ttymidi).
TTYMIDI_BIN = os.environ.get("TTYMIDI_BIN", "ttymidi")
CLIENT_NAME = "ttymidi-e2e"


@pytest.fixture
def bridge() -> Iterator[Bridge]:
    master_fd, slave_fd = os.openpty()
    tty.setraw(master_fd)
    tty.setraw(slave_fd)
    slave_path = os.ttyname(slave_fd)

    proc: subprocess.Popen[bytes] = subprocess.Popen(
        [TTYMIDI_BIN, "-s", slave_path, "-n", CLIENT_NAME, "-v"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    os.close(slave_fd)  # ttymidi now holds its own copy of the slave

    midi_out = rtmidi.MidiOut()
    midi_in = rtmidi.MidiIn()
    # rtmidi drops SysEx / clock / active-sensing by default -- keep them so the
    # test client sees exactly what ttymidi emits.
    midi_in.ignore_types(sysex=False, timing=False, active_sense=False)

    try:
        open_named_port(midi_out, CLIENT_NAME)  # ttymidi's WRITE (input) port
        open_named_port(midi_in, CLIENT_NAME)  # ttymidi's READ (output) port
        time.sleep(0.2)  # let the subscriptions settle
        termios.tcflush(master_fd, termios.TCIOFLUSH)  # drop any startup noise
        yield Bridge(master_fd=master_fd, midi_out=midi_out, midi_in=midi_in, proc=proc)
    finally:
        midi_in.close_port()
        midi_out.close_port()
        midi_in.delete()
        midi_out.delete()
        proc.terminate()
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.close(master_fd)
