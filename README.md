# ttymidi

[![Test](https://github.com/usr-ein/ttymidi/actions/workflows/test.yml/badge.svg)](https://github.com/usr-ein/ttymidi/actions/workflows/test.yml)
[![Build](https://github.com/usr-ein/ttymidi/actions/workflows/build.yml/badge.svg)](https://github.com/usr-ein/ttymidi/actions/workflows/build.yml)

`ttymidi` is a GPL-licensed program that lets external serial devices (such as
an Arduino) interface with the ALSA sequencer, so they can talk to any
ALSA-compatible MIDI software.

> **Original author:** Thiago Teixeira (`tvst@hotmail.com`) —
> original homepage <http://www.varal.org/ttymidi/>.
> This repository is a refactor of that original work, distributed under the
> same GPL license (see [`LICENSE`](LICENSE)).

## Dependencies

`ttymidi` depends on **libasound2** (ALSA). On Debian or Ubuntu, install the
development headers with:

```sh
sudo apt-get install libasound2-dev
```

You also need a C compiler and `make` (`sudo apt-get install build-essential`).

## Building

The source is a single C file. To compile it, just run:

```sh
make
```

This produces a `ttymidi` binary in the current directory.

## Installing

To copy the binary to `/usr/local/bin` for easy access:

```sh
sudo make install
```

To remove it again:

```sh
sudo make uninstall
```

## Cross-building an ARM binary with Docker

If you develop on another platform (e.g. macOS) but want to run `ttymidi` on a
Raspberry Pi, you can build a binary for ARM Linux without a native toolchain.
This requires Docker (with `buildx`, included in Docker Desktop):

```sh
make docker-arm
```

This produces a **fully static** binary at `dist/ttymidi`. Because it is
statically linked (musl + argp + ALSA, all built inside the image), it has no
runtime dependencies and runs on any ARM Linux of the target architecture —
including glibc-based Raspberry Pi OS. Copy it to the Pi and run it directly:

```sh
scp dist/ttymidi pi@raspberrypi:~/
```

The default target is 64-bit (`linux/arm64`, for 64-bit Raspberry Pi OS on a
Pi 3/4/5 or Zero 2). For a 32-bit Pi, override the platform:

```sh
make docker-arm ARM_PLATFORM=linux/arm/v7
```

## Running on a Raspberry Pi

To feed a device connected to the Pi's GPIO UART into ttymidi, the serial port
needs three things: the login console taken off it, the UART hardware enabled,
and your user granted permission to open the device.

### 1. Free the serial port and enable the UART

The easiest way is `raspi-config`:

```sh
sudo raspi-config
```

Go to **Interface Options → Serial Port** and answer:

- *"Would you like a login shell to be accessible over serial?"* → **No**
  (removes the getty/console so it doesn't fight ttymidi for the port)
- *"Would you like the serial port hardware to be enabled?"* → **Yes**

Then reboot.

<details>
<summary>Equivalent manual configuration (for headless/automated setups)</summary>

On Raspberry Pi OS **Bookworm** the boot files live in `/boot/firmware/`; on
older releases they are in `/boot/`.

- Enable the UART in `config.txt`:

  ```
  enable_uart=1
  ```

- On a Pi 3/4/5, the GPIO UART defaults to the low-quality "mini UART" whose
  baud rate drifts with the CPU clock. For a reliable 115200 baud, move the
  full PL011 UART to the GPIO header by also adding:

  ```
  dtoverlay=disable-bt
  ```

  and disable the Bluetooth-modem service:

  ```sh
  sudo systemctl disable --now hciuart
  ```

- Remove the serial console: delete the `console=serial0,115200` token from
  `cmdline.txt`, and disable the login getty:

  ```sh
  sudo systemctl disable --now serial-getty@ttyS0.service
  ```

  (use `serial-getty@ttyAMA0.service` if your UART is `ttyAMA0`).

Reboot for the changes to take effect.
</details>

### 2. Let your user open the serial device

Serial devices belong to the `dialout` group, so add your user to it to use the
port without `sudo`:

```sh
sudo usermod -aG dialout "$USER"
```

Log out and back in (or reboot) for the new group to apply.

### 3. Run ttymidi

`/dev/serial0` is a stable symlink to the Pi's GPIO UART (either `ttyS0` or
`ttyAMA0`, depending on the above):

```sh
ttymidi -s /dev/serial0 -n MyDevice
```

**Set the baud rate to match your device.** ttymidi defaults to `115200`;
the rate must match exactly what your firmware transmits at, or you will get
garbled data or nothing at all. Pass `-b` if it differs (only `1200`, `2400`,
`4800`, `9600`, `19200`, `38400`, `57600`, and `115200` are supported):

```sh
ttymidi -s /dev/serial0 -b 57600 -n MyDevice
```

Note this is separate from the Pi's own UART configuration above — `-b` only
tells ttymidi how to read the port.

Check that the port exists and nothing else is holding it:

```sh
ls -l /dev/serial0
```

To start it automatically at boot, create a small systemd service, e.g.
`/etc/systemd/system/ttymidi.service`:

```ini
[Unit]
Description=ttymidi serial<->ALSA MIDI bridge
After=sound.target

[Service]
ExecStart=/usr/local/bin/ttymidi -s /dev/serial0 -n MyDevice
Restart=on-failure
User=pi

[Install]
WantedBy=multi-user.target
```

Then:

```sh
sudo systemctl enable --now ttymidi
```

## Usage

```
Usage: ttymidi [OPTION...]
ttymidi - Connect serial port devices to ALSA MIDI programs!

  -b, --baudrate=BAUD        Serial port baud rate. Default = 115200
  -n, --name=NAME            Name of the Alsa MIDI client. Default = ttymidi
  -p, --printonly            Super debugging: Print values read from serial --
                             and do nothing else
  -q, --quiet                Don't produce any output, even when the print
                             command is sent
  -s, --serialdevice=DEV     Serial device to use. Default = /dev/ttyUSB0
  -v, --verbose              For debugging: Produce verbose output
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.

Report bugs to ttymidi <at> e1n.sh.
```

<!-- --help prints the real address (ttymidi + '@' + e1n.sh), assembled at
     runtime; it is written obfuscated here so it isn't a scrapable email. -->

Supported baud rates are `1200`, `2400`, `4800`, `9600`, `19200`, `38400`,
`57600`, and `115200`.

### Examples

Connect to `ttyS0` at 2400 bps:

```sh
ttymidi -s /dev/ttyS0 -b 2400
```

Connect to a USB serial port at the default speed (115200 bps) and print
information about incoming MIDI events:

```sh
ttymidi -s /dev/ttyUSB0 -v
```

`ttymidi` creates an ALSA MIDI **output** port that can be connected to any
compatible program. For example, to route it into TiMidity:

```sh
ttymidi -s /dev/ttyUSB0 &   # start ttymidi
timidity -iA &              # start an ALSA-compatible MIDI program
aconnect -i                 # list available MIDI input clients
aconnect -o                 # list available MIDI output clients
aconnect 128:0 129:0        # 128 and 129 are the client numbers for
                            # ttymidi and timidity respectively
```

It also creates an ALSA MIDI **input** port that feeds incoming MIDI events back
out to the serial port.

If you prefer a GUI to connect MIDI clients, tools like `qjackctl` work well.

### The device name: ALSA client vs. ports (`-n`)

ALSA's sequencer has two levels of naming that are easy to confuse:

- a **client** — the application, i.e. ttymidi — has a name, and
- each client owns one or more **ports**, and every port has its *own* name.

ttymidi runs as one client with two ports: an **output** port (serial → ALSA —
what your DAW/host reads from) and an **input** port (ALSA → serial — what
carries MIDI back to the device, e.g. LED feedback). `ttymidi -n NAME` sets the
client name **and** both port names to `NAME`.

The distinction matters because most MIDI hosts (Mixxx, qjackctl, a DAW's device
list) identify and show a device by its **port** name, not the client name. The
original ttymidi only set the *client* name and left the ports hardcoded as
`MIDI out` / `MIDI in`, so hosts displayed the device as *"MIDI out"* no matter
what `-n` you passed. This version names the ports after `-n` too, so your
chosen name is what appears:

```sh
ttymidi -s /dev/serial0 -n TriMixxx
aconnect -l                 # client 'TriMixxx', with ports also named 'TriMixxx'
```

A couple of consequences:

- Both ports share the same name, which is fine — ALSA addresses ports by their
  numeric `client:port` id, and the two are further distinguished by direction
  (read vs. write). A host that renders a device as `client:port` may show it as
  `TriMixxx:TriMixxx`.
- For the **input** direction to work (host → device), ttymidi advertises its
  ports as generic MIDI devices so hosts will route MIDI *to* them. If a host
  only offers ttymidi as an input and never as an output, that advertisement is
  what's missing — make sure you're on this version.

## MIDI protocol support

ttymidi implements the parts of MIDI that make sense over a point-to-point
serial link and deliberately leaves out the rest. Here is what it does with each
category of message.

### Channel-voice messages — fully supported

These carry a status byte (`command + channel`) followed by one or two data
bytes:

| byte1     | byte2                  | byte3                 | Command name                         |
| --------- | ---------------------- | --------------------- | ------------------------------------ |
| 0x80–0x8F | Key # (0–127)          | Off velocity (0–127)  | Note OFF                             |
| 0x90–0x9F | Key # (0–127)          | On velocity (0–127)   | Note ON                              |
| 0xA0–0xAF | Key # (0–127)          | Pressure (0–127)      | Poly Key Pressure                    |
| 0xB0–0xBF | Control # (0–127)      | Control value (0–127) | Control Change                       |
| 0xC0–0xCF | Program # (0–127)      | *(none — 2 bytes)*    | Program Change                       |
| 0xD0–0xDF | Pressure value (0–127) | *(none — 2 bytes)*    | Mono Key Pressure (Channel Pressure) |
| 0xE0–0xEF | Range LSB (0–127)      | Range MSB (0–127)     | Pitch Bend                           |

Byte #1 is `COMMAND + CHANNEL` — e.g. `0xE3` is Pitch Bend (`0xE0`) on channel 4
(`0x03`). Channels are 0–15.

- **Variable length:** Program Change (`0xC0`) and Channel Pressure (`0xD0`)
  carry a single data byte, so they are **2 bytes on the wire** — do *not* pad
  them with a third byte.
- **Running status (input):** on the serial → ALSA path, a status byte may be
  followed by the data of several messages; ttymidi reuses the last status, as
  standard MIDI allows. (On the ALSA → serial path it always writes a full
  status byte — see "Running status on output" below.)

### System Real-Time — passed through

The single-byte messages `0xF8`–`0xFE` (timing clock, start/continue/stop,
active sensing) may appear anywhere in the stream, even between the data bytes
of another message. ttymidi forwards them to ALSA without disturbing the message
in progress, so clock/transport sync survives the bridge.

> `0xFF` is the exception: ttymidi keeps it as the escape for its non-MIDI
> **comment messages** (`0xFF 0x00 0x00 <len> <text>`), *not* MIDI System Reset.

### System Exclusive (SysEx) — carried opaquely, both directions

SysEx messages (`0xF0 … 0xF7`) are forwarded end to end. On serial → ALSA,
ttymidi reassembles the whole message and hands it to ALSA as a single SysEx
event; on ALSA → serial it writes the raw bytes straight to the device. ttymidi
does **not** interpret the payload — the manufacturer ID, layout, and meaning
are entirely between the host and the device. It is a transparent pipe.

**Why this was added: RGB LED feedback from Mixxx.** The motivating use case is
driving addressable RGB LEDs on a DJ controller from [Mixxx](https://mixxx.org)
over the serial/UART bridge. Channel-voice messages can't express a color: a
Note-On velocity is a single 7-bit value (fine for brightness or an on/off lamp,
not a 24-bit R/G/B triple), and spreading a color across three CCs updates the
LED non-atomically and burns three controller numbers per LED. Mixxx therefore
recommends SysEx for LED output — a controller mapping emits one SysEx per LED
carrying the full color, e.g.

    F0 <manufacturer-id> <led> <r> <g> <b> F7

with every payload byte 7-bit (`0x00`–`0x7F`), as SysEx requires (`0x7D` is the
reserved non-commercial/educational manufacturer ID, handy for a private
device). ttymidi now carries that message unchanged to the firmware; the format
itself lives in your firmware and Mixxx mapping.

Worth knowing:

- **Bounded buffer.** A message is reassembled into a fixed 256-byte buffer
  (`MIDI_SYSEX_MAX`, markers included) — ample for a per-LED command or a
  whole-ring batch. Anything longer is dropped whole rather than truncated, so
  the device never sees a half-message.
- **Real-time stays live.** System Real-Time bytes (`0xF8`–`0xFE`) interleaved
  inside a SysEx still pass through untouched, so clock/transport sync is not
  disturbed by a large color update.
- **Clean abort.** Any other status byte arriving mid-SysEx ends it without a
  terminator: the partial message is discarded and normal parsing resumes.

### Not supported (by design)

- **System Common** (`0xF1`–`0xF6`: MTC quarter-frame, song position/select,
  tune request). These are variable-length, rarely emitted by controllers, and
  meaningless to a serial-attached instrument — supporting them would add parser
  states for no practical gain.
- **Running status on output.** On ALSA → serial, ttymidi always emits a full
  status byte per message. Running status is a bandwidth optimization from the
  31250-baud DIN-MIDI era; over a 115200+ UART the one saved byte isn't worth
  requiring every receiving firmware to implement running-status parsing — the
  same reasoning as fixing the baud rate rather than autodetecting it.

## License

GPL — see [`LICENSE`](LICENSE).
