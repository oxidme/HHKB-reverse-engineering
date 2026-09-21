# The HHKB vendor HID protocol

Everything here was measured against a `PD-KB401B` (HHKB Professional Classic,
US layout) running firmware A4.29, on macOS 27.0.

The wire protocol was first worked out by
[happy-hacking-gnu](https://gitlab.com/dom/happy-hacking-gnu), whose source
comments reference symbol names from PFU's own tool. This document re-derives it
against a Classic — a model happy-hacking-gnu does not cover — and corrects two
of its names along the way.

## Interfaces

`04FE:0020`, `bcdDevice` 1, full speed, one configuration, three interfaces:

| # | Class | Endpoints | Contents |
|---|---|---|---|
| 0 | HID boot keyboard (subclass 1, protocol 1) | 1 | the standard 8-byte 6KRO report and LED output |
| 1 | HID (subclass 0) | 1 | report 1: 8 consumer bits; report 2: a 232-bit NKRO bitmap; report 3: 6 application-launch bits |
| 2 | HID, vendor usage page `0xFF00` | 2 | the control channel |

Interface 2 is what the Keymap Tool talks to. Its report descriptor is 34 bytes:
usage page `0xFF00`, usage `0x01`, one 64-byte input report and one 64-byte
output report, no report IDs.

QMK and VIA use `0xFF60` / usage `0x61` for raw HID; PFU picked its own. Match on
the usage page rather than the interface number.

Interface 1 is where the media keys actually come out — the keymap can name
them, but they are sent as consumer usages, not keyboard usages.

## Framing

Requests and responses are always exactly 64 bytes, zero padded.

**Request (host to keyboard, output report)**

| Offset | Contents |
|---|---|
| 0–1 | `AA AA` |
| 2 | command |
| 3 | chunk marker (`0x00` for single-packet commands) |
| 4 | payload length |
| 5.. | payload |

**Response (keyboard to host, input report)**

| Offset | Contents |
|---|---|
| 0–1 | `55 55` |
| 2 | command, echoed |
| 3 | status |
| 4 | chunk marker |
| 5 | payload length |
| 6.. | payload |

The request payload starts at offset 5 and the response payload at offset 6,
because the status byte only exists on the response. Aligning the two is the
easiest way to end up one byte out.

### Status byte

| Value | Meaning |
|---|---|
| `0x00` | success |
| `0x01` | rejected — the board is in the read-only state that `DUMP_FIRMWARE` leaves behind |

Status `0x01` is not just a failed write. Reads in that state can return values
that were never written, so nothing you read after seeing it means anything
until the keyboard has been replugged. See [firmware.md](firmware.md).

### Chunk markers

Commands that span several packets number them `(n << 6) | n`:

| n | Marker |
|---|---|
| 1 | `0x41` |
| 2 | `0x82` |
| 3 | `0xC3` |

The same counter appears in both the top two bits and the bottom six. The
redundancy is presumably a consistency check.

## Commands

| ID | Name | Verified | Notes |
|---|---|:---:|---|
| `0x01` | `NOTIFY_APPLICATION_STATE` | | payload `00 01 <state>`, 0 on open and 1 on close. Everything works without it |
| `0x02` | `GET_KEYBOARD_INFO` | yes | returns a 57-byte struct |
| `0x03` | `RESET_FACTORY_DEFAULTS` | yes | restores every mode and both layers |
| `0x04` | `CONFIRM_KEYMAP` | yes | commits a keymap write |
| `0x05` | `GET_DIP_STATE` | yes | six DIP switch states |
| `0x06` | `GET_KEYBOARD_MODE` | yes | 0 HHK, 1 Mac, 2 Win |
| `0x07` | `RESET_DIPSW` | | happy-hacking-gnu calls this after a remap; not needed here |
| `0x86` | `WRITE_KEYMAP` | yes | three chunks, then `CONFIRM_KEYMAP` |
| `0x87` | `GET_KEYMAP` | yes | three chunks; the mode parameter is honoured |
| `0xD0` | `DUMP_FIRMWARE` | yes | returns 64 KiB, and leaves the board read-only |
| `0xE0`–`0xE3` | `FIRMUP_*` | | updates the application bank |
| `0xE4`–`0xE7` | `UPDATEBOOT_*` | | rewrites the backup bank. Do not |

`NOTIFY_APPLICATION_STATE` is sent by the official tool but is not a
precondition for anything: reads, writes and commits all succeed without it.

## `GET_KEYBOARD_INFO`

57 bytes of payload:

| Offset in response | Length | Field |
|---|---|---|
| 6 | 20 | type number, NUL terminated |
| 26 | 4 | revision |
| 30 | 16 | serial |
| 46 | 8 | application firmware version (bank 2) |
| 54 | 8 | boot firmware version (bank 1) |
| 62 | 1 | which one is running: 0 application, 1 boot |

Version fields lead with `0x0A` or `0x0B`, naming the A and B banks. Formatting
them as `%X%d.%d%d` gives the versions the official tool shows — `A4.29` and
`B4.16` on the board used here.

happy-hacking-gnu identifies models from the type number: one containing `20` is
JIS, one containing `KB8` is a HYBRID. `PD-KB401B` is neither, being a US
Classic.

## Modes

`GET_KEYBOARD_MODE` returns 0 for HHK, 1 for Mac and 2 for Win, selected by the
first two DIP switches:

| SW1 | SW2 | Mode |
|---|---|---|
| off | off | HHK (factory setting) |
| on | off | Win |
| off | on | Mac |

There is no mode 3. The firmware holds three keymap tables, the manual documents
three settings, and asking for a fourth times out with no reply at all.

happy-hacking-gnu calls mode 2 "Lite" and mode 3 "Secret". Neither name appears
in the Professional Classic manual (`P3PC-6661-06`); mode 2 is Win, and mode 3
does not exist on this model.

The mode also moves without touching the DIP switches: `Fn`+`Control`+`W` and
`Fn`+`Control`+`M` select Win and Mac, though a board whose DIP switches say HHK
returns to HHK. `GET_KEYBOARD_MODE` reports the effective mode.

The remaining switches: SW3 chooses whether the Delete key sends Delete or
Backspace and is ignored in Mac mode; SW4 turns the left diamond into Fn.

## Keymaps

One layer is a 128-byte array indexed by key number. Keys 1 to 60 are real; index
0 and 61 onward are always zero. Key 1 is the bottom right key and numbering runs
right to left and bottom to top, so Esc is 60.

There are two layers — base and Fn — held separately for each of the three modes,
which is why both reads and writes carry a mode.

### Transfer

128 bytes across three packets, split differently in each direction:

| | Chunk 1 | Chunk 2 | Chunk 3 |
|---|---|---|---|
| read (responses) | 58 bytes | 58 bytes | 12 bytes |
| write (requests) | mode, layer, 57 bytes | 59 bytes | 12 bytes |

On a read the mode and layer ride in the request, with payload length 2. On a
write they occupy the first two payload bytes of chunk 1, which is why that
chunk carries fewer keymap bytes than chunk 2.

**A read requests only the first chunk.** The keyboard pushes the other two on
its own. Code that waits for one response per request will drop them.

A write ends with `CONFIRM_KEYMAP`. The change takes effect immediately, with no
replug.

**The per-chunk status byte does not tell you whether a write landed.** Read the
keymap back and compare.

### Keycodes

Values are USB HID keyboard usages, with these exceptions:

| Code | Meaning |
|---|---|
| `0x01` | Fn — a firmware-internal code, not HID ErrorRollOver |
| `0x78` | Stop. HHK mode puts this on the right diamond in the Fn layer |
| `0x8A` | Henkan (International4). HHK mode only |
| `0x8B` | Muhenkan (International5). HHK mode only |
| `0xE8` | Volume Down. Mac mode only |
| `0xE9` | Volume Up. Mac mode only |
| `0xEA` | Mute. Mac mode only |
| `0xEB` | Eject. Mac mode only |

`0xE8`–`0xEB` are reserved in the HID keyboard page; the firmware converts them
into consumer usages on interface 1. The mapping is a lookup table rather than
arithmetic — they land on consumer bits 5, 6, 4 and 7. Assigning them elsewhere
is not obviously safe.

HHK mode puts Henkan and Muhenkan on the diamond keys, which are the Japanese
input toggles. Mac and Win modes put the GUI modifiers there instead.

See [keymaps.md](keymaps.md) for the factory contents of all six layers.

## Talking to it from macOS

No hidapi, no libusb. `IOHIDManager` is enough, and the result is a native arm64
binary with no third-party dependency.

1. Match on `kIOHIDVendorIDKey == 0x04FE` and
   `kIOHIDPrimaryUsagePageKey == 0xFF00`. That picks interface 2 uniquely.
2. Send with `IOHIDDeviceSetReport(kIOHIDReportTypeOutput, 0, buf, 64)`.
3. Replies arrive as **input reports**, not as `GetReport`. Register an input
   report callback, schedule the device on a run loop, and pump the run loop.
4. **No Input Monitoring permission is needed.** `ioreg` marks interfaces 0 and 1
   `RequiresTCCAuthorization`; interface 2 is not marked, and no prompt appears.
5. `IOHIDDeviceSetReport` has no timeout of its own and blocks indefinitely once
   the board is read-only. Bound the whole operation instead — the tools here use
   a 60 second watchdog.

Porting from happy-hacking-gnu means shifting every buffer index by one: hidapi
prepends a report ID byte, so its `buffer[1]` is the first byte on the wire.
