# ttymidi

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

Report bugs to tvst@hotmail.com.
```

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

## ttymidi message specification

Every MIDI command is sent over the serial port as **3 bytes**. The first byte
holds the command type and channel; the next two are parameter bytes. To keep
decoding simple, `ttymidi` does not support "running status" and always forces
commands into 3 bytes — so even single-parameter commands must send byte #3 (as
a `0`).

| byte1     | byte2                   | byte3                  | Command name                          |
| --------- | ----------------------- | ---------------------- | ------------------------------------- |
| 0x80–0x8F | Key # (0–127)           | Off velocity (0–127)   | Note OFF                              |
| 0x90–0x9F | Key # (0–127)           | On velocity (0–127)    | Note ON                               |
| 0xA0–0xAF | Key # (0–127)           | Pressure (0–127)       | Poly Key Pressure                     |
| 0xB0–0xBF | Control # (0–127)       | Control value (0–127)  | Control Change                        |
| 0xC0–0xCF | Program # (0–127)       | Not used (send 0)      | Program Change                        |
| 0xD0–0xDF | Pressure value (0–127)  | Not used (send 0)      | Mono Key Pressure (Channel Pressure)  |
| 0xE0–0xEF | Range LSB (0–127)       | Range MSB (0–127)      | Pitch Bend                            |
| 0xF0–0xFF | Manufacturer's ID       | Model ID               | System *(not implemented)*            |

Byte #1 is `COMMAND + CHANNEL`. For example, `0xE3` is the Pitch Bend command
(`0xE0`) on channel 4 (`0x03`). Channels range from 0 to 15.

## License

GPL — see [`LICENSE`](LICENSE).
