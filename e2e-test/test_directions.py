"""End-to-end checks of both bridge directions, including SysEx (the reason the
feature exists) and the oversized-drop recovery behaviour."""

from __future__ import annotations

import os

from harness import Bridge, read_exact, recv_message

# --- PTY -> ALSA (device -> host) -------------------------------------------


def test_note_on_pty_to_alsa(bridge: Bridge) -> None:
    os.write(bridge.master_fd, bytes([0x90, 60, 64]))
    assert recv_message(bridge.midi_in) == [0x90, 60, 64]


def test_running_status_pty_to_alsa(bridge: Bridge) -> None:
    os.write(bridge.master_fd, bytes([0x90, 60, 64, 62, 65]))
    assert recv_message(bridge.midi_in) == [0x90, 60, 64]
    assert recv_message(bridge.midi_in) == [0x90, 62, 65]


def test_sysex_pty_to_alsa(bridge: Bridge) -> None:
    message = [0xF0, 0x7D, 0x01, 0x02, 0x03, 0x04, 0xF7]
    os.write(bridge.master_fd, bytes(message))
    assert recv_message(bridge.midi_in) == message


def test_oversized_sysex_dropped_then_recovers(bridge: Bridge) -> None:
    os.write(bridge.master_fd, bytes([0xF0, 0x7D, *([0x01] * 300), 0xF7]))
    os.write(bridge.master_fd, bytes([0x90, 60, 64]))
    # The oversized SysEx is dropped whole; the note after it must still arrive.
    assert recv_message(bridge.midi_in) == [0x90, 60, 64]


# --- ALSA -> PTY (host -> device) -------------------------------------------


def test_note_on_alsa_to_pty(bridge: Bridge) -> None:
    bridge.midi_out.send_message([0x90, 60, 64])
    assert read_exact(bridge.master_fd, 3) == bytes([0x90, 60, 64])


def test_sysex_alsa_to_pty(bridge: Bridge) -> None:
    message = [0xF0, 0x7D, 0x0A, 0x0B, 0x0C, 0xF7]
    bridge.midi_out.send_message(message)
    assert read_exact(bridge.master_fd, len(message)) == bytes(message)


def test_large_sysex_alsa_to_pty(bridge: Bridge) -> None:
    # ~120-byte payload, every byte 7-bit: exercises ALSA fragmentation + write_all.
    message = [0xF0, 0x7D, *range(120), 0xF7]
    bridge.midi_out.send_message(message)
    assert read_exact(bridge.master_fd, len(message)) == bytes(message)
