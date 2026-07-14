/* End-to-end tests: feed real ttymidi captures (from a Raspberry Pi) through
   our parser + decoder and check they frame and decode correctly.

   The byte streams and their expected message composition come from
   tests/fixtures/ via gen_dump_fixtures.py -- the expectations are counted in
   Python, independently of the C code under test. */

#include "dump_fixtures.h"
#include "greatest.h"
#include "midi.h"

typedef struct
{
    int total, cc, note_on, note_off, other;
} counts_t;

/*
 * Drive `bytes` through the parser one byte at a time, decoding every frame.
 * Each emitted event must line up with its 3-byte message in the stream (all
 * messages in these captures are 3 bytes: status + two data bytes). Returns -1
 * if every event matched, otherwise the index of the first message that did
 * not. Fills *out with the message composition.
 */
static int check_stream(const unsigned char* bytes, int len, counts_t* out)
{
    midi_parser_t p;
    midi_parser_init(&p);
    counts_t c = {0, 0, 0, 0, 0};

    for (int i = 0; i < len; i++)
    {
        unsigned char frame[3];
        if (!midi_parser_push(&p, bytes[i], frame))
            continue;

        midi_event_t ev = midi_decode(frame);
        int base        = c.total * 3;

        if (base + 2 >= len)
            return c.total;
        if (midi_kind_status(ev.kind) != (bytes[base] & 0xF0))
            return c.total;
        if (ev.channel != (bytes[base] & 0x0F))
            return c.total;
        if (ev.param1 != bytes[base + 1])
            return c.total;
        if (ev.param2 != bytes[base + 2])
            return c.total;

        switch (ev.kind)
        {
        case MIDI_CONTROL_CHANGE:
            c.cc++;
            break;
        case MIDI_NOTE_ON:
            c.note_on++;
            break;
        case MIDI_NOTE_OFF:
            c.note_off++;
            break;
        default:
            c.other++;
            break;
        }
        c.total++;
    }

    *out = c;
    return -1;
}

TEST serial_print_capture(void)
{
    counts_t c;
    int bad = check_stream(serial_print_bytes, serial_print_bytes_len, &c);
    ASSERT_EQ_FMTm("event did not match its raw 3-byte message", -1, bad, "%d");
    ASSERT_EQ(SERIAL_PRINT_MSGS, c.total);
    ASSERT_EQ(SERIAL_PRINT_CC, c.cc);
    ASSERT_EQ(SERIAL_PRINT_NOTE_ON, c.note_on);
    ASSERT_EQ(SERIAL_PRINT_NOTE_OFF, c.note_off);
    ASSERT_EQ(0, c.other);
    PASS();
}

TEST verbose_capture(void)
{
    counts_t c;
    int bad = check_stream(verbose_bytes, verbose_bytes_len, &c);
    ASSERT_EQ_FMTm("event did not match its raw 3-byte message", -1, bad, "%d");
    ASSERT_EQ(VERBOSE_MSGS, c.total);
    ASSERT_EQ(VERBOSE_CC, c.cc);
    ASSERT_EQ(VERBOSE_NOTE_ON, c.note_on);
    ASSERT_EQ(VERBOSE_NOTE_OFF, c.note_off);
    ASSERT_EQ(0, c.other);
    PASS();
}

/* Spot-check concrete decoded values so a silent shift can't hide behind the
   aggregate counts. The captures are from the TriMixxx deck: CC 0x11 is JOG. */
TEST serial_print_first_message(void)
{
    unsigned char frame[3];
    midi_parser_t p;
    midi_parser_init(&p);
    int got = 0;
    for (int i = 0; i < serial_print_bytes_len && !got; i++)
        got = midi_parser_push(&p, serial_print_bytes[i], frame);

    ASSERT(got);
    midi_event_t ev = midi_decode(frame);
    ASSERT_EQ(MIDI_CONTROL_CHANGE, ev.kind);
    ASSERT_EQ(0, ev.channel);
    ASSERT_EQ(0x11, ev.param1); /* CC_JOG */
    ASSERT_EQ(0x01, ev.param2);
    PASS();
}

SUITE(dump_suite)
{
    RUN_TEST(serial_print_capture);
    RUN_TEST(verbose_capture);
    RUN_TEST(serial_print_first_message);
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(dump_suite);
    GREATEST_MAIN_END();
}
