"""Helpers for the ttymidi end-to-end tests: the bridge handle plus the
timeout-bounded read helpers. The pytest fixture that wires them together lives
in conftest.py."""

from __future__ import annotations

import os
import select
import subprocess
import time
from dataclasses import dataclass

import rtmidi

PORT_TIMEOUT_S = 10.0
RECV_TIMEOUT_S = 5.0


@dataclass
class Bridge:
    """One running ttymidi instance and the two ends we test it through."""

    master_fd: int  # our side of the PTY; ttymidi holds the slave
    midi_out: rtmidi.MidiOut  # sends into ttymidi's ALSA input port (ALSA -> PTY)
    midi_in: rtmidi.MidiIn  # receives ttymidi's ALSA output (PTY -> ALSA)
    proc: subprocess.Popen[bytes]


def open_named_port(
    port: rtmidi.MidiIn | rtmidi.MidiOut, needle: str, timeout: float = PORT_TIMEOUT_S
) -> None:
    """Wait for a ttymidi port whose name contains `needle`, then open it.

    MidiOut lists only write-capable ports and MidiIn only read-capable ones, so
    the same-named ttymidi ports resolve to the correct direction automatically.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for index, name in enumerate(port.get_ports()):
            if needle in name:
                port.open_port(index)
                return
        time.sleep(0.05)
    raise TimeoutError(f"ttymidi port {needle!r} never appeared")


def read_exact(fd: int, n: int, timeout: float = RECV_TIMEOUT_S) -> bytes:
    """Read exactly n bytes from fd, or return what arrived before the timeout."""
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        readable, _, _ = select.select([fd], [], [], remaining)
        if readable:
            buf += os.read(fd, n - len(buf))
    return bytes(buf)


def recv_message(midi_in: rtmidi.MidiIn, timeout: float = RECV_TIMEOUT_S) -> list[int] | None:
    """Return the next MIDI message as a list of bytes, or None on timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        message = midi_in.get_message()
        if message is not None:
            data, _delta = message
            return list(data)
        time.sleep(0.005)
    return None
