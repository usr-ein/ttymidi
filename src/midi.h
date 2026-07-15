/*
    This file is part of ttymidi.

    ttymidi is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ttymidi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ttymidi.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Pure MIDI translation logic, with no dependency on ALSA or serial I/O so it
 * can be unit tested in isolation. ttymidi.c wires these functions to the
 * actual devices; everything worth testing lives here.
 */

#ifndef TTYMIDI_MIDI_H
#define TTYMIDI_MIDI_H

/* Channel-voice message kinds ttymidi understands. */
typedef enum
{
    MIDI_NOTE_OFF,         /* 0x80 */
    MIDI_NOTE_ON,          /* 0x90 */
    MIDI_KEY_PRESSURE,     /* 0xA0  polyphonic key pressure */
    MIDI_CONTROL_CHANGE,   /* 0xB0 */
    MIDI_PROGRAM_CHANGE,   /* 0xC0  single data byte */
    MIDI_CHANNEL_PRESSURE, /* 0xD0  single data byte */
    MIDI_PITCH_BEND,       /* 0xE0 */
    MIDI_UNKNOWN           /* anything else (e.g. system messages) */
} midi_kind_t;

/*
 * A decoded MIDI message.
 *
 * For most kinds param1/param2 are the two 7-bit data bytes. PROGRAM_CHANGE and
 * CHANNEL_PRESSURE use only param1 (param2 is 0). PITCH_BEND stores the signed
 * 14-bit bend in param1 (range -8192..8191, matching ALSA's convention);
 * param2 is 0.
 */
typedef struct
{
    midi_kind_t kind;
    int channel; /* 0..15 */
    int param1;
    int param2;
} midi_event_t;

/* Status nibble (0x80, 0x90, ...) for a kind, or 0x00 for MIDI_UNKNOWN. */
unsigned char midi_kind_status(midi_kind_t kind);

/* Decode a 3-byte frame (status + two data bytes) into an event. */
midi_event_t midi_decode(const unsigned char frame[3]);

/*
 * Encode an event into raw MIDI bytes. Writes into out[3] and returns the
 * number of bytes to transmit (2 for PROGRAM_CHANGE / CHANNEL_PRESSURE, 3
 * otherwise), or 0 if the event cannot be encoded (MIDI_UNKNOWN).
 */
int midi_encode(const midi_event_t* ev, unsigned char out[3]);

/*
 * Streaming frame parser (running-status aware).
 *
 * Reassembles the serial byte stream into complete 3-byte frames, mirroring
 * ttymidi's historical behaviour: data bytes are ignored until the first status
 * byte, the running status is kept across messages, any status byte resyncs the
 * parser, and 0xC0/0xD0 are treated as single-data-byte commands (the emitted
 * frame's third byte is 0 for those).
 *
 * System Exclusive (0xF0..0xF7) is reassembled whole into the sysex[] buffer and
 * carried opaquely -- ttymidi does not interpret its contents.
 */

/*
 * Largest SysEx (bytes, including the 0xF0 and 0xF7 markers) the parser will
 * buffer. Our RGB LED commands are ~8 bytes and a whole-ring batch update is
 * ~200, so 256 leaves headroom; longer messages are dropped (midi_parser_push).
 */
#define MIDI_SYSEX_MAX 256

typedef struct
{
    unsigned char status; /* current running status, 0 = not yet synced */
    unsigned char data[2];
    int ndata;

    /* SysEx reassembly. in_sysex is true between a 0xF0 and its terminator;
       sysex[] holds the message so far, including the leading 0xF0. */
    int in_sysex;
    int sysex_len;
    unsigned char sysex[MIDI_SYSEX_MAX];
} midi_parser_t;

void midi_parser_init(midi_parser_t* p);

/* Outcome of feeding one byte to the parser. */
typedef enum
{
    MIDI_PARSE_NONE = 0, /* nothing to emit yet (more bytes needed) */
    MIDI_PARSE_MESSAGE,  /* frame[3] holds a complete channel-voice message */
    MIDI_PARSE_REALTIME, /* frame[0] holds a System Real-Time byte to pass through */
    MIDI_PARSE_SYSEX     /* p->sysex[0..p->sysex_len) holds a complete SysEx message */
} midi_parse_result_t;

/*
 * Feed one byte.
 *
 * Returns MIDI_PARSE_MESSAGE and fills frame[3] when a full channel-voice
 * message is ready; MIDI_PARSE_REALTIME with the byte in frame[0] for a System
 * Real-Time message (0xF8..0xFE), which may interleave anywhere and does not
 * disturb the running status; MIDI_PARSE_NONE otherwise.
 *
 * Returns MIDI_PARSE_SYSEX once a full System Exclusive message (0xF0..0xF7) has
 * been reassembled into p->sysex (p->sysex_len bytes, both markers included);
 * read it straight from the parser struct. A Real-Time byte interleaved inside a
 * SysEx still passes through as MIDI_PARSE_REALTIME without corrupting it; any
 * other status byte aborts the SysEx, and a message longer than MIDI_SYSEX_MAX
 * is dropped.
 *
 * 0xFF is deliberately NOT treated as real-time (System Reset): it stays a
 * status byte so ttymidi's 0xFF 0x00 0x00 comment-message escape keeps working.
 */
midi_parse_result_t midi_parser_push(midi_parser_t* p, unsigned char byte, unsigned char frame[3]);

#endif /* TTYMIDI_MIDI_H */
