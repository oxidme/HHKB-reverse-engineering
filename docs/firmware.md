# Firmware

What `DUMP_FIRMWARE` (`0xD0`) returns on a `PD-KB401B` running A4.29, and what
can be read out of it.

**No firmware image is redistributed here.** `hhkb_fwdump` reads one off your own
keyboard; the result is PFU's copyrighted work and is excluded from this
repository. What follows is a description of its structure.

## It leaves the board read-only

`DUMP_FIRMWARE` is a one-way transition. Until the keyboard is unplugged and
replugged:

| | |
|---|---|
| USB enumeration | all three interfaces stay up |
| key presses | **not reported at all** |
| `GET_KEYBOARD_INFO` and friends | still answer |
| `GET_KEYMAP` | answers, but **the values cannot be trusted** |
| `WRITE_KEYMAP` | rejected with status `0x01` |
| a second `DUMP_FIRMWARE` | no reply, and the vendor channel wedges |

Nothing is written and nothing stored changes — firmware versions and keymaps
all survive intact. Only key scanning stops.

The unreliable reads are the part that bites. After a rejected write, a
`GET_KEYMAP` returned a value that had never been written; a restore that
verified itself against that read reported success while the board had in fact
kept the experimental value, which only surfaced after replugging. **Treat
everything you read after a status `0x01` as meaningless until the board has
been replugged.**

A second dump attempt is worse: `IOHIDDeviceSetReport` blocks with no timeout,
and after killing the process the channel stays broken — every request fails
with `0xE00002D6` and stale packets sit in the queue. Only a replug clears it.

Have another input device to hand before running this.

## Transfer

The request is just `AA AA D0 00 00`. The keyboard then pushes packets
unprompted:

| Offset | Contents |
|---|---|
| 0–5 | `55 55 D0 00 00 <len>` |
| 6–7 | packet counter, **big endian** |
| 8.. | firmware bytes, `len - 2` of them |

Up to 56 bytes per packet. The first packet shorter than 56 bytes ends the
stream.

Measured: 1171 packets, counters 0 through 1170, 65536 bytes exactly, trailing
`0xFF` padding.

## The image

**Plain code. No encryption, no obfuscation.** Entropy is about 6.06 bits per
byte, with large runs of `0xFF` and `0x00`.

It opens with an ARM Cortex-M vector table. The initial stack pointer is
`0x200020B8`, in SRAM, and the reset vector is `0x080100D5` — Thumb, so the
handler is at `0x080100D4`.

`0x08000000` is the start of flash on an STM32, which puts **the image's load
address at `0x08010000`**: 64 KiB into flash. That lines up with the two banks
`GET_KEYBOARD_INFO` reports, with the boot firmware at `0x08000000` and the
application firmware at `0x08010000`.

### The MCU

The peripheral addresses the code references point at the **STM32F1** family, or
a compatible part:

| Address | Peripheral |
|---|---|
| `0x40021000` | RCC |
| `0x40022000` | flash interface |
| `0x40010800` | GPIOA |
| `0x40010000` | AFIO / EXTI |
| `0x40005C00` | USB full-speed device |
| `0x40007000` | PWR |

Nothing references the `0x48000000` GPIO block that the F0 and L0 families use.

## Factory keymap tables

Six 128-byte keymaps sit at `0xCEE8`, spaced 144 bytes apart. They match what the
board reports byte for byte.

| Offset | Layer |
|---|---|
| `0xCEE8` | HHK, base |
| `0xCF78` | HHK, Fn |
| `0xD008` | Win, base |
| `0xD098` | Win, Fn |
| `0xD128` | Mac, base |
| `0xD1B8` | Mac, Fn |

**The stored order is HHK, Win, Mac** — not the mode numbering, which is HHK 0,
Mac 1, Win 2. Do not treat the table index as a mode ID.

The last 16 bytes of each 144-byte slot are zero.

**There is no seventh slot**, which is why `GET_KEYMAP` with mode 3 times out:
there is no fourth table to return, and the manual documents only three settings.

A similar structure near `0xCD5C` appears to be the JIS variant, with its 69-key
layout. Not analysed.

These are the factory defaults — the source `RESET_FACTORY_DEFAULTS` restores
from, not the live keymap.

## The live keymap is not in here

Changing a key and dumping again produces a byte-identical image.

| Dump | State | SHA-256 |
|---|---|---|
| A | factory | `8e7efb83…` |
| B | factory, a later USB session | `8e7efb83…` |
| C | one key changed | `8e7efb83…` |

A and B matching establishes that the dump is deterministic across sessions, so
C matching too means the keymap is **outside the dumped range** rather than the
dump being insensitive to it.

`DUMP_FIRMWARE` returns only the 64 KiB of application code at `0x08010000`.
Whatever `WRITE_KEYMAP` writes to — a separate flash page, emulated EEPROM —
is not reachable this way.

## HID report descriptors

The keyboard descriptor is near `0xDE20` and the consumer descriptor near
`0xDE63`, but **not verbatim**. Comparing against what the device actually
reports, bytes around the zeros are substituted, so some encoding or compression
is applied. The scheme has not been worked out.

The consumer usage order for report 1 is readable regardless:

| Bit | Usage | |
|---|---|---|
| 0 | `0x00CD` | Play/Pause |
| 1 | `0x00B7` | Stop |
| 2 | `0x00B6` | Scan Previous Track |
| 3 | `0x00B5` | Scan Next Track |
| 4 | `0x00E2` | Mute |
| 5 | `0x00EA` | Volume Decrement |
| 6 | `0x00E9` | Volume Increment |
| 7 | `0x00B8` | Eject |

### Which consumer bits `0xE8`–`0xEB` reach

| Code | Default position | Function | Consumer bit |
|---|---|---|---|
| `0xE8` | Mac Fn+A | Volume Down | 5 |
| `0xE9` | Mac Fn+S | Volume Up | 6 |
| `0xEA` | Mac Fn+D | Mute | 4 |
| `0xEB` | Mac Fn+F | Eject | 7 |

**Not arithmetic.** Treating `code - 0xE8` as the bit index gives the wrong
answer; the real order is 5, 6, 4, 7, so the firmware must hold an explicit
table. The codes do run in physical key order across A, S, D and F.

Eject does nothing on a Mac with no optical drive, which is what "Fn+F appears
dead" means — and is what pinned bit 7 down.

**No transport controls are reachable from the default keymap.** Nothing maps to
consumer bits 0 through 3 in any mode or layer. HHK mode does carry `0x78`, the
keyboard-page Stop, on the right diamond in its Fn layer.

## Not worked out

- Where the live keymap actually lives. `DUMP_FIRMWARE` cannot reach it, and it
  takes no address argument.
- The descriptor encoding.
- The JIS table near `0xCD5C`.
- The contents of the boot bank. `DUMP_FIRMWARE` appears to return only the
  running application image.
- The exact STM32F1 part and its total flash. If it were exactly 128 KiB the two
  banks would fill it, leaving nowhere for the keymap, so a larger part seems
  likely.
