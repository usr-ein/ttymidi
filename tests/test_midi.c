/* Unit tests for the pure MIDI logic in src/midi.c (no ALSA / serial). */

#include "greatest.h"
#include "midi.h"

/* --------------------------------------------------------------------- */
/* midi_decode: raw serial frame -> event                                */

TEST decode_note_on(void)
{
    unsigned char frame[3] = {0x91, 60, 64};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_NOTE_ON, ev.kind);
    ASSERT_EQ(1, ev.channel);
    ASSERT_EQ(60, ev.param1);
    ASSERT_EQ(64, ev.param2);
    PASS();
}

TEST decode_note_off(void)
{
    unsigned char frame[3] = {0x82, 60, 10};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_NOTE_OFF, ev.kind);
    ASSERT_EQ(2, ev.channel);
    ASSERT_EQ(60, ev.param1);
    ASSERT_EQ(10, ev.param2);
    PASS();
}

TEST decode_key_pressure(void)
{
    unsigned char frame[3] = {0xA3, 60, 20};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_KEY_PRESSURE, ev.kind);
    ASSERT_EQ(3, ev.channel);
    PASS();
}

TEST decode_control_change(void)
{
    unsigned char frame[3] = {0xB0, 7, 100};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_CONTROL_CHANGE, ev.kind);
    ASSERT_EQ(0, ev.channel);
    ASSERT_EQ(7, ev.param1);
    ASSERT_EQ(100, ev.param2);
    PASS();
}

TEST decode_program_change_ignores_param2(void)
{
    /* Third byte is not meaningful for program change; must be reported as 0. */
    unsigned char frame[3] = {0xC5, 42, 99};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_PROGRAM_CHANGE, ev.kind);
    ASSERT_EQ(5, ev.channel);
    ASSERT_EQ(42, ev.param1);
    ASSERT_EQ(0, ev.param2);
    PASS();
}

TEST decode_channel_pressure(void)
{
    unsigned char frame[3] = {0xD4, 55, 0};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_CHANNEL_PRESSURE, ev.kind);
    ASSERT_EQ(4, ev.channel);
    ASSERT_EQ(55, ev.param1);
    ASSERT_EQ(0, ev.param2);
    PASS();
}

TEST decode_pitch_bend_center(void)
{
    /* LSB=0, MSB=64 -> 8192, centered -> 0. */
    unsigned char frame[3] = {0xE0, 0x00, 0x40};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_PITCH_BEND, ev.kind);
    ASSERT_EQ(0, ev.param1);
    PASS();
}

TEST decode_pitch_bend_extremes(void)
{
    unsigned char lo[3] = {0xE2, 0x00, 0x00};
    unsigned char hi[3] = {0xE2, 0x7F, 0x7F};
    midi_event_t evlo   = midi_decode(lo);
    midi_event_t evhi   = midi_decode(hi);
    ASSERT_EQ(-8192, evlo.param1);
    ASSERT_EQ(8191, evhi.param1);
    ASSERT_EQ(2, evhi.channel);
    PASS();
}

TEST decode_system_is_unknown(void)
{
    unsigned char frame[3] = {0xF0, 1, 2};
    midi_event_t ev        = midi_decode(frame);
    ASSERT_EQ(MIDI_UNKNOWN, ev.kind);
    PASS();
}

/* --------------------------------------------------------------------- */
/* midi_encode: event -> raw serial frame                                */

TEST encode_note_on(void)
{
    midi_event_t ev = {MIDI_NOTE_ON, 1, 60, 64};
    unsigned char out[3];
    ASSERT_EQ(3, midi_encode(&ev, out));
    ASSERT_EQ(0x91, out[0]);
    ASSERT_EQ(60, out[1]);
    ASSERT_EQ(64, out[2]);
    PASS();
}

TEST encode_program_change_is_two_bytes(void)
{
    midi_event_t ev = {MIDI_PROGRAM_CHANGE, 5, 42, 0};
    unsigned char out[3];
    ASSERT_EQ(2, midi_encode(&ev, out));
    ASSERT_EQ(0xC5, out[0]);
    ASSERT_EQ(42, out[1]);
    PASS();
}

TEST encode_channel_pressure_is_two_bytes(void)
{
    midi_event_t ev = {MIDI_CHANNEL_PRESSURE, 4, 55, 0};
    unsigned char out[3];
    ASSERT_EQ(2, midi_encode(&ev, out));
    ASSERT_EQ(0xD4, out[0]);
    ASSERT_EQ(55, out[1]);
    PASS();
}

TEST encode_pitch_bend(void)
{
    midi_event_t center = {MIDI_PITCH_BEND, 0, 0, 0};
    midi_event_t lo     = {MIDI_PITCH_BEND, 0, -8192, 0};
    midi_event_t hi     = {MIDI_PITCH_BEND, 0, 8191, 0};
    unsigned char out[3];

    ASSERT_EQ(3, midi_encode(&center, out));
    ASSERT_EQ(0xE0, out[0]);
    ASSERT_EQ(0x00, out[1]);
    ASSERT_EQ(0x40, out[2]); /* 8192 >> 7 */

    midi_encode(&lo, out);
    ASSERT_EQ(0x00, out[1]);
    ASSERT_EQ(0x00, out[2]);

    midi_encode(&hi, out);
    ASSERT_EQ(0x7F, out[1]);
    ASSERT_EQ(0x7F, out[2]);
    PASS();
}

TEST encode_masks_data_to_7_bits(void)
{
    midi_event_t ev = {MIDI_NOTE_ON, 0, 200, 130};
    unsigned char out[3];
    midi_encode(&ev, out);
    ASSERT_EQ(200 & 0x7F, out[1]);
    ASSERT_EQ(130 & 0x7F, out[2]);
    PASS();
}

TEST encode_unknown_returns_zero(void)
{
    midi_event_t ev = {MIDI_UNKNOWN, 0, 0, 0};
    unsigned char out[3];
    ASSERT_EQ(0, midi_encode(&ev, out));
    PASS();
}

/* Round-trip: decode(encode(x)) preserves channel-voice messages. */
TEST round_trip_channel_voice(void)
{
    midi_kind_t kinds[] = {MIDI_NOTE_OFF, MIDI_NOTE_ON,
                           MIDI_KEY_PRESSURE, MIDI_CONTROL_CHANGE,
                           MIDI_PROGRAM_CHANGE, MIDI_CHANNEL_PRESSURE,
                           MIDI_PITCH_BEND};
    for (unsigned k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
    {
        midi_event_t in = {kinds[k], 7, (kinds[k] == MIDI_PITCH_BEND) ? 1234 : 42, 99};
        if (kinds[k] == MIDI_PROGRAM_CHANGE || kinds[k] == MIDI_CHANNEL_PRESSURE)
            in.param2 = 0;

        unsigned char raw[3] = {0, 0, 0};
        int n                = midi_encode(&in, raw);
        ASSERT(n == 2 || n == 3);

        midi_event_t out = midi_decode(raw);
        ASSERT_EQ(in.kind, out.kind);
        ASSERT_EQ(in.channel, out.channel);
        ASSERT_EQ(in.param1, out.param1);
        if (n == 3 && in.kind != MIDI_PITCH_BEND)
            ASSERT_EQ(in.param2, out.param2);
    }
    PASS();
}

/* --------------------------------------------------------------------- */
/* midi_parser: byte stream -> frames                                    */

/* Feed a byte sequence through the parser and collect the emitted frames. */
static int feed(const unsigned char* bytes, int n, unsigned char frames[][3])
{
    midi_parser_t p;
    midi_parser_init(&p);
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        unsigned char frame[3];
        if (midi_parser_push(&p, bytes[i], frame) == MIDI_PARSE_MESSAGE)
        {
            frames[count][0] = frame[0];
            frames[count][1] = frame[1];
            frames[count][2] = frame[2];
            count++;
        }
    }
    return count;
}

TEST parse_single_message(void)
{
    unsigned char in[] = {0x90, 60, 64};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0x90, f[0][0]);
    ASSERT_EQ(60, f[0][1]);
    ASSERT_EQ(64, f[0][2]);
    PASS();
}

TEST parse_ignores_leading_data_bytes(void)
{
    /* Junk data before the first status byte must be dropped. */
    unsigned char in[] = {0x10, 0x20, 0x90, 60, 64};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0x90, f[0][0]);
    PASS();
}

TEST parse_running_status(void)
{
    /* Two notes sharing one status byte (running status). */
    unsigned char in[] = {0x90, 60, 64, 62, 65};
    unsigned char f[8][3];
    ASSERT_EQ(2, feed(in, sizeof(in), f));
    ASSERT_EQ(0x90, f[1][0]);
    ASSERT_EQ(62, f[1][1]);
    ASSERT_EQ(65, f[1][2]);
    PASS();
}

TEST parse_two_full_messages(void)
{
    unsigned char in[] = {0x90, 60, 64, 0x80, 60, 0};
    unsigned char f[8][3];
    ASSERT_EQ(2, feed(in, sizeof(in), f));
    ASSERT_EQ(0x90, f[0][0]);
    ASSERT_EQ(0x80, f[1][0]);
    PASS();
}

TEST parse_single_data_byte_command(void)
{
    /* Program change is complete after one data byte; third byte is 0. */
    unsigned char in[] = {0xC0, 5};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0xC0, f[0][0]);
    ASSERT_EQ(5, f[0][1]);
    ASSERT_EQ(0, f[0][2]);
    PASS();
}

TEST parse_single_data_byte_running_status(void)
{
    unsigned char in[] = {0xC0, 5, 6};
    unsigned char f[8][3];
    ASSERT_EQ(2, feed(in, sizeof(in), f));
    ASSERT_EQ(5, f[0][1]);
    ASSERT_EQ(6, f[1][1]);
    PASS();
}

TEST parse_resync_on_status_mid_message(void)
{
    /* A status byte arriving mid-message discards the partial data. */
    unsigned char in[] = {0x90, 60, 0x80, 48, 16};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0x80, f[0][0]);
    ASSERT_EQ(48, f[0][1]);
    ASSERT_EQ(16, f[0][2]);
    PASS();
}

TEST parse_new_status_replaces_previous(void)
{
    unsigned char in[] = {0x90, 0xB0, 7, 100};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0xB0, f[0][0]);
    ASSERT_EQ(7, f[0][1]);
    ASSERT_EQ(100, f[0][2]);
    PASS();
}

TEST parse_comment_frame(void)
{
    /* The 0xFF 0x00 0x00 non-MIDI marker is emitted as an ordinary frame
       for the caller to recognise. */
    unsigned char in[] = {0xFF, 0x00, 0x00};
    unsigned char f[8][3];
    ASSERT_EQ(1, feed(in, sizeof(in), f));
    ASSERT_EQ(0xFF, f[0][0]);
    ASSERT_EQ(0x00, f[0][1]);
    ASSERT_EQ(0x00, f[0][2]);
    PASS();
}

/* --------------------------------------------------------------------- */
/* System Real-Time pass-through                                         */

TEST realtime_single_byte(void)
{
    midi_parser_t p;
    midi_parser_init(&p);
    unsigned char f[3];
    unsigned char codes[] = {0xF8, 0xFA, 0xFB, 0xFC, 0xFE}; /* clock/start/cont/stop/sensing */
    for (unsigned i = 0; i < sizeof(codes); i++)
    {
        ASSERT_EQ(MIDI_PARSE_REALTIME, midi_parser_push(&p, codes[i], f));
        ASSERT_EQ(codes[i], f[0]);
    }
    PASS();
}

TEST realtime_interleaved_does_not_corrupt_message(void)
{
    /* A real-time byte arriving between the data bytes of a note must pass
       through without disturbing the message in progress. */
    midi_parser_t p;
    midi_parser_init(&p);
    unsigned char f[3];
    ASSERT_EQ(MIDI_PARSE_NONE, midi_parser_push(&p, 0x90, f)); /* status */
    ASSERT_EQ(MIDI_PARSE_NONE, midi_parser_push(&p, 60, f));   /* data 1 */
    ASSERT_EQ(MIDI_PARSE_REALTIME, midi_parser_push(&p, 0xF8, f));
    ASSERT_EQ(0xF8, f[0]);
    ASSERT_EQ(MIDI_PARSE_MESSAGE, midi_parser_push(&p, 64, f)); /* data 2 completes it */
    ASSERT_EQ(0x90, f[0]);
    ASSERT_EQ(60, f[1]);
    ASSERT_EQ(64, f[2]);
    PASS();
}

TEST realtime_preserves_running_status(void)
{
    midi_parser_t p;
    midi_parser_init(&p);
    unsigned char f[3];
    midi_parser_push(&p, 0x90, f);
    midi_parser_push(&p, 60, f);
    ASSERT_EQ(MIDI_PARSE_MESSAGE, midi_parser_push(&p, 64, f));    /* first note */
    ASSERT_EQ(MIDI_PARSE_REALTIME, midi_parser_push(&p, 0xFE, f)); /* sensing between messages */
    midi_parser_push(&p, 62, f);
    ASSERT_EQ(MIDI_PARSE_MESSAGE, midi_parser_push(&p, 65, f)); /* running status still 0x90 */
    ASSERT_EQ(0x90, f[0]);
    ASSERT_EQ(62, f[1]);
    ASSERT_EQ(65, f[2]);
    PASS();
}

TEST realtime_ff_is_comment_not_reset(void)
{
    /* 0xFF stays the comment-message escape, not a real-time System Reset. */
    midi_parser_t p;
    midi_parser_init(&p);
    unsigned char f[3];
    ASSERT_EQ(MIDI_PARSE_NONE, midi_parser_push(&p, 0xFF, f));
    ASSERT_EQ(MIDI_PARSE_NONE, midi_parser_push(&p, 0x00, f));
    ASSERT_EQ(MIDI_PARSE_MESSAGE, midi_parser_push(&p, 0x00, f));
    ASSERT_EQ(0xFF, f[0]);
    PASS();
}

SUITE(realtime_suite)
{
    RUN_TEST(realtime_single_byte);
    RUN_TEST(realtime_interleaved_does_not_corrupt_message);
    RUN_TEST(realtime_preserves_running_status);
    RUN_TEST(realtime_ff_is_comment_not_reset);
}

SUITE(decode_suite)
{
    RUN_TEST(decode_note_on);
    RUN_TEST(decode_note_off);
    RUN_TEST(decode_key_pressure);
    RUN_TEST(decode_control_change);
    RUN_TEST(decode_program_change_ignores_param2);
    RUN_TEST(decode_channel_pressure);
    RUN_TEST(decode_pitch_bend_center);
    RUN_TEST(decode_pitch_bend_extremes);
    RUN_TEST(decode_system_is_unknown);
}

SUITE(encode_suite)
{
    RUN_TEST(encode_note_on);
    RUN_TEST(encode_program_change_is_two_bytes);
    RUN_TEST(encode_channel_pressure_is_two_bytes);
    RUN_TEST(encode_pitch_bend);
    RUN_TEST(encode_masks_data_to_7_bits);
    RUN_TEST(encode_unknown_returns_zero);
    RUN_TEST(round_trip_channel_voice);
}

SUITE(parser_suite)
{
    RUN_TEST(parse_single_message);
    RUN_TEST(parse_ignores_leading_data_bytes);
    RUN_TEST(parse_running_status);
    RUN_TEST(parse_two_full_messages);
    RUN_TEST(parse_single_data_byte_command);
    RUN_TEST(parse_single_data_byte_running_status);
    RUN_TEST(parse_resync_on_status_mid_message);
    RUN_TEST(parse_new_status_replaces_previous);
    RUN_TEST(parse_comment_frame);
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(decode_suite);
    RUN_SUITE(encode_suite);
    RUN_SUITE(parser_suite);
    RUN_SUITE(realtime_suite);
    GREATEST_MAIN_END();
}
