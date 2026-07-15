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

#include "midi.h"

/* Number of data bytes a status byte expects: 0xC0/0xD0 take one, rest two. */
static int status_data_len(unsigned char status)
{
    unsigned char op = status & 0xF0;
    return (op == 0xC0 || op == 0xD0) ? 1 : 2;
}

unsigned char midi_kind_status(midi_kind_t kind)
{
    switch (kind)
    {
    case MIDI_NOTE_OFF:
        return 0x80;
    case MIDI_NOTE_ON:
        return 0x90;
    case MIDI_KEY_PRESSURE:
        return 0xA0;
    case MIDI_CONTROL_CHANGE:
        return 0xB0;
    case MIDI_PROGRAM_CHANGE:
        return 0xC0;
    case MIDI_CHANNEL_PRESSURE:
        return 0xD0;
    case MIDI_PITCH_BEND:
        return 0xE0;
    default:
        return 0x00;
    }
}

midi_event_t midi_decode(const unsigned char frame[3])
{
    midi_event_t ev;
    unsigned char op = frame[0] & 0xF0;

    ev.channel = frame[0] & 0x0F;
    ev.param1  = frame[1];
    ev.param2  = frame[2];

    switch (op)
    {
    case 0x80:
        ev.kind = MIDI_NOTE_OFF;
        break;
    case 0x90:
        ev.kind = MIDI_NOTE_ON;
        break;
    case 0xA0:
        ev.kind = MIDI_KEY_PRESSURE;
        break;
    case 0xB0:
        ev.kind = MIDI_CONTROL_CHANGE;
        break;
    case 0xC0:
        ev.kind   = MIDI_PROGRAM_CHANGE;
        ev.param2 = 0;
        break;
    case 0xD0:
        ev.kind   = MIDI_CHANNEL_PRESSURE;
        ev.param2 = 0;
        break;
    case 0xE0:
        ev.kind   = MIDI_PITCH_BEND;
        ev.param1 = (frame[1] & 0x7F) + ((frame[2] & 0x7F) << 7) - 8192;
        ev.param2 = 0;
        break;
    default:
        ev.kind = MIDI_UNKNOWN;
        break;
    }

    return ev;
}

int midi_encode(const midi_event_t* ev, unsigned char out[3])
{
    unsigned char status = midi_kind_status(ev->kind) | (ev->channel & 0x0F);

    switch (ev->kind)
    {
    case MIDI_NOTE_OFF:
    case MIDI_NOTE_ON:
    case MIDI_KEY_PRESSURE:
    case MIDI_CONTROL_CHANGE:
        out[0] = status;
        out[1] = ev->param1 & 0x7F;
        out[2] = ev->param2 & 0x7F;
        return 3;
    case MIDI_PROGRAM_CHANGE:
    case MIDI_CHANNEL_PRESSURE:
        out[0] = status;
        out[1] = ev->param1 & 0x7F;
        return 2;
    case MIDI_PITCH_BEND:
    {
        int v  = ev->param1 + 8192;
        out[0] = status;
        out[1] = v & 0x7F;
        out[2] = (v >> 7) & 0x7F;
        return 3;
    }
    default:
        return 0;
    }
}

void midi_parser_init(midi_parser_t* p)
{
    p->status    = 0;
    p->ndata     = 0;
    p->in_sysex  = 0;
    p->sysex_len = 0;
}

midi_parse_result_t midi_parser_push(midi_parser_t* p, unsigned char byte, unsigned char frame[3])
{
    if (byte >= 0xF8 && byte != 0xFF)
    {
        /* System Real-Time (0xF8..0xFE): a single byte that may appear anywhere,
           even between the data bytes of another message or inside a SysEx. Pass
           it through without touching any message in progress. 0xFF is left to
           the normal path so it can start a comment message. */
        frame[0] = byte;
        return MIDI_PARSE_REALTIME;
    }

    if (byte == 0xF0)
    {
        /* Start of System Exclusive. A status byte clears running status; begin
           accumulating into p->sysex, keeping the leading 0xF0 marker. */
        p->status                = 0;
        p->ndata                 = 0;
        p->in_sysex              = 1;
        p->sysex_len             = 0;
        p->sysex[p->sysex_len++] = 0xF0;
        return MIDI_PARSE_NONE;
    }

    if (p->in_sysex)
    {
        if (byte == 0xF7)
        {
            /* End of SysEx (EOX). On overflow there is no room for the closing
               0xF7, so drop the whole (truncated) message rather than forward it. */
            p->in_sysex = 0;
            if (p->sysex_len < MIDI_SYSEX_MAX)
            {
                p->sysex[p->sysex_len++] = 0xF7;
                return MIDI_PARSE_SYSEX;
            }
            return MIDI_PARSE_NONE;
        }
        if (byte & 0x80)
        {
            /* Any other status byte ends the SysEx abnormally (no EOX): discard
               the partial message and reprocess this byte as a fresh status. */
            p->in_sysex  = 0;
            p->sysex_len = 0;
        }
        else
        {
            /* SysEx data byte: append while there is room. On overflow we stop
               storing but keep scanning, so the EOX above drops the message. */
            if (p->sysex_len < MIDI_SYSEX_MAX)
                p->sysex[p->sysex_len++] = byte;
            return MIDI_PARSE_NONE;
        }
    }

    if (byte == 0xF7)
    {
        /* Stray EOX outside any SysEx: ignore. (Also keeps 0xF7 out of the
           status-resync branch below, since it has its high bit set.) */
        return MIDI_PARSE_NONE;
    }

    if (byte & 0x80)
    {
        /* Status byte: (re)sync, dropping any partially collected data. */
        p->status = byte;
        p->ndata  = 0;
        return MIDI_PARSE_NONE;
    }

    if (p->status == 0)
    {
        /* Data byte before we have ever seen a status byte: ignore it. */
        return MIDI_PARSE_NONE;
    }

    p->data[p->ndata++] = byte;
    if (p->ndata < status_data_len(p->status))
        return MIDI_PARSE_NONE;

    frame[0] = p->status;
    frame[1] = p->data[0];
    frame[2] = (status_data_len(p->status) == 2) ? p->data[1] : 0;
    p->ndata = 0; /* keep running status for the next message */
    return MIDI_PARSE_MESSAGE;
}
