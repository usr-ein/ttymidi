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


#include <alsa/asoundlib.h>
#include <argp.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
// Linux-specific
#include <asm/ioctls.h>
#include <linux/ioctl.h>
#include <linux/serial.h>

#include "midi.h"

#define FALSE 0
#define TRUE 1

#define MAX_DEV_STR_LEN 32
#define MAX_MSG_SIZE 1024

/* change this definition for the correct port */
// #define _POSIX_SOURCE 1 /* POSIX compliant source */

int run;
int serial;
int port_out_id;

/* --------------------------------------------------------------------- */
// Program options

static struct argp_option options[] =
    {
        {"serialdevice", 's', "DEV", 0, "Serial device to use. Default = /dev/ttyUSB0"},
        {"baudrate", 'b', "BAUD", 0, "Serial port baud rate. Default = 115200"},
        {"verbose", 'v', 0, 0, "For debugging: Produce verbose output"},
        {"printonly", 'p', 0, 0, "Super debugging: Print values read from serial -- and do nothing else"},
        {"quiet", 'q', 0, 0, "Don't produce any output, even when the print command is sent"},
        {"name", 'n', "NAME", 0, "Name of the Alsa MIDI client. Default = ttymidi"},
        {0}};

typedef struct _arguments
{
    int silent, verbose, printonly;
    char serialdevice[MAX_DEV_STR_LEN];
    int baudrate;
    char name[MAX_DEV_STR_LEN];
} arguments_t;

static void exit_cli(int sig)
{
    run = FALSE;
    printf("\rttymidi closing down ... ");
}

static error_t parse_opt(int key, char* arg, struct argp_state* state)
{
    /* Get the input argument from argp_parse, which we
       know is a pointer to our arguments structure. */
    arguments_t* arguments = state->input;
    int baud_temp;

    switch (key)
    {
    case 'p':
        arguments->printonly = 1;
        break;
    case 'q':
        arguments->silent = 1;
        break;
    case 'v':
        arguments->verbose = 1;
        break;
    case 's':
        if (arg == NULL)
            break;
        strncpy(arguments->serialdevice, arg, MAX_DEV_STR_LEN);
        break;
    case 'n':
        if (arg == NULL)
            break;
        strncpy(arguments->name, arg, MAX_DEV_STR_LEN);
        break;
    case 'b':
        if (arg == NULL)
            break;
        baud_temp = strtol(arg, NULL, 0);
        if (baud_temp != EINVAL && baud_temp != ERANGE)
            switch (baud_temp)
            {
            case 1200:
                arguments->baudrate = B1200;
                break;
            case 2400:
                arguments->baudrate = B2400;
                break;
            case 4800:
                arguments->baudrate = B4800;
                break;
            case 9600:
                arguments->baudrate = B9600;
                break;
            case 19200:
                arguments->baudrate = B19200;
                break;
            case 38400:
                arguments->baudrate = B38400;
                break;
            case 57600:
                arguments->baudrate = B57600;
                break;
            case 115200:
                arguments->baudrate = B115200;
                break;
            default:
                printf("Baud rate %i is not supported.\n", baud_temp);
                exit(1);
            }

    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static void arg_set_defaults(arguments_t* arguments)
{
    const char* serialdevice_temp = "/dev/ttyUSB0";
    arguments->printonly          = 0;
    arguments->silent             = 0;
    arguments->verbose            = 0;
    arguments->baudrate           = B115200;
    const char* name_tmp          = "ttymidi";
    strncpy(arguments->serialdevice, serialdevice_temp, MAX_DEV_STR_LEN);
    strncpy(arguments->name, name_tmp, MAX_DEV_STR_LEN);
}

const char* argp_program_version = "ttymidi 0.60";
/* Assembled at runtime in main() from split parts (64 == '@') so the maintainer
   address is never stored as a scrapable "user@host" literal. */
const char* argp_program_bug_address = NULL;
static char doc[]                    = "ttymidi - Connect serial port devices to ALSA MIDI programs!";
static struct argp argp              = {options, parse_opt, 0, doc};
arguments_t arguments;


/* --------------------------------------------------------------------- */
// MIDI stuff

static int open_seq(snd_seq_t** seq)
{
    int port_out_id;

    if (snd_seq_open(seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
    {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }

    snd_seq_set_client_name(*seq, arguments.name);

    /* Name the ports after the client so hosts display the chosen device name
       (e.g. -n TriMixxx) rather than a fixed port label. ALSA identifies ports
       by numeric client:port id, so giving both the same name is harmless.

       Advertise them as generic MIDI devices (SND_SEQ_PORT_TYPE_MIDI_GENERIC),
       as python-rtmidi's open_virtual_port() does: without it many hosts (e.g.
       Mixxx) won't offer the input port as an output target, so host->device
       feedback never reaches the serial line. */
    if ((port_out_id = snd_seq_create_simple_port(*seq, arguments.name,
                                                  SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                                  SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION)) < 0)
    {
        fprintf(stderr, "Error creating sequencer port.\n");
    }

    if (snd_seq_create_simple_port(*seq, arguments.name,
                                   SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                   SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION) < 0)
    {
        fprintf(stderr, "Error creating sequencer port.\n");
    }

    return port_out_id;
}

/* Human-readable trace of one MIDI event, for -v. `src` is "Serial" or "Alsa"
   to show which side the event came from. */
static void log_midi_event(const char* src, const midi_event_t* ev)
{
    if (arguments.silent || !arguments.verbose)
        return;

    unsigned char op = midi_kind_status(ev->kind);
    switch (ev->kind)
    {
    case MIDI_NOTE_OFF:
        printf("%s 0x%x Note off           %03d %03d %03d\n", src, op, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_NOTE_ON:
        printf("%s 0x%x Note on            %03d %03d %03d\n", src, op, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_KEY_PRESSURE:
        printf("%s 0x%x Pressure change    %03d %03d %03d\n", src, op, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_CONTROL_CHANGE:
        printf("%s 0x%x Controller change  %03d %03d %03d\n", src, op, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_PROGRAM_CHANGE:
        printf("%s 0x%x Program change     %03d %03d\n", src, op, ev->channel, ev->param1);
        break;
    case MIDI_CHANNEL_PRESSURE:
        printf("%s 0x%x Channel change     %03d %03d\n", src, op, ev->channel, ev->param1);
        break;
    case MIDI_PITCH_BEND:
        printf("%s 0x%x Pitch bend         %03d %05d\n", src, op, ev->channel, ev->param1);
        break;
    default:
        printf("%s 0x%x Unknown MIDI cmd   %03d %03d %03d\n", src, op, ev->channel, ev->param1, ev->param2);
        break;
    }
}

/* Thin adapter: push a decoded MIDI event out to the ALSA sequencer. */
static void emit_alsa_event(snd_seq_t* seq, int out_port, const midi_event_t* ev)
{
    log_midi_event("Serial", ev);

    snd_seq_event_t sev;
    snd_seq_ev_clear(&sev);
    snd_seq_ev_set_direct(&sev);
    snd_seq_ev_set_source(&sev, out_port);
    snd_seq_ev_set_subs(&sev);

    switch (ev->kind)
    {
    case MIDI_NOTE_OFF:
        snd_seq_ev_set_noteoff(&sev, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_NOTE_ON:
        snd_seq_ev_set_noteon(&sev, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_KEY_PRESSURE:
        snd_seq_ev_set_keypress(&sev, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_CONTROL_CHANGE:
        snd_seq_ev_set_controller(&sev, ev->channel, ev->param1, ev->param2);
        break;
    case MIDI_PROGRAM_CHANGE:
        snd_seq_ev_set_pgmchange(&sev, ev->channel, ev->param1);
        break;
    case MIDI_CHANNEL_PRESSURE:
        snd_seq_ev_set_chanpress(&sev, ev->channel, ev->param1);
        break;
    case MIDI_PITCH_BEND:
        snd_seq_ev_set_pitchbend(&sev, ev->channel, ev->param1);
        break;
    default:
        return; /* MIDI_UNKNOWN / system messages: nothing to forward */
    }

    snd_seq_event_output_direct(seq, &sev);
    snd_seq_drain_output(seq);
}

/* Translate one incoming ALSA event into our MIDI event representation.
   Returns 1 on success, 0 for event types we don't forward. */
static int alsa_event_to_midi(const snd_seq_event_t* ev, midi_event_t* out)
{
    out->channel = ev->data.control.channel;
    out->param1  = 0;
    out->param2  = 0;

    switch (ev->type)
    {
    case SND_SEQ_EVENT_NOTEOFF:
        out->kind   = MIDI_NOTE_OFF;
        out->param1 = ev->data.note.note;
        out->param2 = ev->data.note.velocity;
        return 1;
    case SND_SEQ_EVENT_NOTEON:
        out->kind   = MIDI_NOTE_ON;
        out->param1 = ev->data.note.note;
        out->param2 = ev->data.note.velocity;
        return 1;
    case SND_SEQ_EVENT_KEYPRESS:
        out->kind   = MIDI_KEY_PRESSURE;
        out->param1 = ev->data.note.note;
        out->param2 = ev->data.note.velocity;
        return 1;
    case SND_SEQ_EVENT_CONTROLLER:
        out->kind   = MIDI_CONTROL_CHANGE;
        out->param1 = ev->data.control.param;
        out->param2 = ev->data.control.value;
        return 1;
    case SND_SEQ_EVENT_PGMCHANGE:
        out->kind   = MIDI_PROGRAM_CHANGE;
        out->param1 = ev->data.control.value;
        return 1;
    case SND_SEQ_EVENT_CHANPRESS:
        out->kind   = MIDI_CHANNEL_PRESSURE;
        out->param1 = ev->data.control.value;
        return 1;
    case SND_SEQ_EVENT_PITCHBEND:
        out->kind   = MIDI_PITCH_BEND;
        out->param1 = ev->data.control.value; /* ALSA gives a signed -8192..8191 bend */
        return 1;
    default:
        return 0;
    }
}

static void write_midi_action_to_serial_port(snd_seq_t* seq_handle)
{
    snd_seq_event_t* ev;

    do
    {
        snd_seq_event_input(seq_handle, &ev);

        midi_event_t m;
        int have = alsa_event_to_midi(ev, &m);
        snd_seq_free_event(ev);

        if (!have)
            continue;

        unsigned char bytes[3];
        int n = midi_encode(&m, bytes);
        if (n > 0)
        {
            log_midi_event("Alsa", &m);
            write(serial, bytes, n);
        }

    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
}


static void* read_midi_from_alsa(void* seq)
{
    int npfd;
    struct pollfd* pfd;
    snd_seq_t* seq_handle;

    seq_handle = seq;

    npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    pfd  = (struct pollfd*)alloca(npfd * sizeof(struct pollfd));
    snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);

    while (run)
    {
        if (poll(pfd, npfd, 100) > 0)
        {
            write_midi_action_to_serial_port(seq_handle);
        }
    }

    printf("\nStopping [PC]->[Hardware] communication...");
    return NULL;
}

static void* read_midi_from_serial_port(void* seq)
{
    unsigned char byte, frame[3], msg[MAX_MSG_SIZE];
    midi_parser_t parser;
    midi_parser_init(&parser);

    while (run)
    {
        /* super-debug mode: only echo whatever comes through the serial port. */
        if (arguments.printonly)
        {
            if (read(serial, &byte, 1) != 1)
                continue;
            printf("%x\t", byte);
            fflush(stdout);
            continue;
        }

        /* Feed the byte stream through the framer until a message is ready. */
        if (read(serial, &byte, 1) != 1)
            continue;
        if (!midi_parser_push(&parser, byte, frame))
            continue;

        /* Non-MIDI "comment" messages start with 0xFF 0x00 0x00, followed by a
           length byte and that many text bytes -- read out of band here. */
        if (frame[0] == 0xFF && frame[1] == 0x00 && frame[2] == 0x00)
        {
            unsigned char len = 0;
            int msglen;

            if (read(serial, &len, 1) != 1)
                continue;
            msglen = len;
            if (msglen > MAX_MSG_SIZE - 1)
                msglen = MAX_MSG_SIZE - 1;
            if (read(serial, msg, msglen) != msglen)
                continue;

            midi_parser_init(&parser); /* text bytes bypassed the framer */

            if (arguments.silent)
                continue;

            msg[msglen] = 0;
            puts("0xFF Non-MIDI message: ");
            puts((char*)msg);
            putchar('\n');
            fflush(stdout);
            continue;
        }

        midi_event_t ev = midi_decode(frame);
        emit_alsa_event(seq, port_out_id, &ev);
    }

    return NULL;
}

/* --------------------------------------------------------------------- */
// Main program

int main(int argc, char** argv)
{
    struct termios oldtio, newtio;
    snd_seq_t* seq;

    /* Assemble the maintainer address at runtime; 64 == '@'. Keeping the parts
       apart means no plain-text "user@host" ever appears in the source or the
       binary for email-harvesting bots to scrape. argp reads this global while
       formatting --help, so it must be set before argp_parse(). */
    char bug_address[24];
    snprintf(bug_address, sizeof(bug_address), "%s%c%s", "ttymidi", 64, "e1n.sh");
    argp_program_bug_address = bug_address;

    arg_set_defaults(&arguments);
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    /*
     * Open MIDI output port
     */

    port_out_id = open_seq(&seq);

    /*
     *  Open modem device for reading and not as controlling tty because we don't
     *  want to get killed if linenoise sends CTRL-C.
     */

    serial = open(arguments.serialdevice, O_RDWR | O_NOCTTY);

    if (serial < 0)
    {
        perror(arguments.serialdevice);
        exit(-1);
    }

    /* save current serial port settings */
    tcgetattr(serial, &oldtio);

    /* clear struct for new port settings */
    bzero(&newtio, sizeof(newtio));

    /*
     * BAUDRATE : Set bps rate. You could also use cfsetispeed and cfsetospeed.
     * CRTSCTS  : output hardware flow control (only used if the cable has
     * all necessary lines. See sect. 7 of Serial-HOWTO)
     * CS8      : 8n1 (8bit, no parity, 1 stopbit)
     * CLOCAL   : local connection, no modem contol
     * CREAD    : enable receiving characters
     */
    newtio.c_cflag = arguments.baudrate | CS8 | CLOCAL | CREAD; // CRTSCTS removed

    /*
     * IGNPAR  : ignore bytes with parity errors
     * ICRNL   : map CR to NL (otherwise a CR input on the other computer
     * will not terminate input)
     * otherwise make device raw (no other input processing)
     */
    newtio.c_iflag = IGNPAR;

    /* Raw output */
    newtio.c_oflag = 0;

    /*
     * ICANON  : enable canonical input
     * disable all echo functionality, and don't send signals to calling program
     */
    newtio.c_lflag = 0; // non-canonical

    /*
     * set up: we'll be reading 4 bytes at a time.
     */
    newtio.c_cc[VTIME] = 0; /* inter-character timer unused */
    newtio.c_cc[VMIN]  = 1; /* blocking read until n character arrives */

    /*
     * now clean the modem line and activate the settings for the port
     */
    tcflush(serial, TCIFLUSH);
    tcsetattr(serial, TCSANOW, &newtio);

    // Linux-specific: enable low latency mode (FTDI "nagling off")
    //	ioctl(serial, TIOCGSERIAL, &ser_info);
    //	ser_info.flags |= ASYNC_LOW_LATENCY;
    //	ioctl(serial, TIOCSSERIAL, &ser_info);

    if (arguments.printonly)
    {
        printf("Super debug mode: Only printing the signal to screen. Nothing else.\n");
    }

    /*
     * read commands
     */

    /* Starting thread that is polling alsa midi in port */
    pthread_t midi_out_thread, midi_in_thread;
    run = TRUE;
    pthread_create(&midi_out_thread, NULL, read_midi_from_alsa, (void*)seq);
    /* And also thread for polling serial data. As serial is currently read in
           blocking mode, by this we can enable ctrl+c quiting and avoid zombie
           alsa ports when killing app with ctrl+z */
    pthread_create(&midi_in_thread, NULL, read_midi_from_serial_port, (void*)seq);
    signal(SIGINT, exit_cli);
    signal(SIGTERM, exit_cli);

    while (run)
    {
        sleep(100);
    }

    void* status;
    pthread_join(midi_out_thread, &status);

    /* restore the old port settings */
    tcsetattr(serial, TCSANOW, &oldtio);
    printf("\ndone!\n");

    return 0;
}
