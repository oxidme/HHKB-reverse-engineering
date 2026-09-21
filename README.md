# hhkb-native

Native Apple Silicon tools for the HHKB Professional Classic, built on macOS's own
IOKit. No Rosetta, no third-party dependencies.

PFU's official HHKB Keymap Tool for Mac is an x86_64 build, so it runs under
Rosetta on Apple Silicon. It also refuses to edit keymaps on Classic models at
all. Both of those are properties of the tool, not of the keyboard: the firmware
answers keymap reads and writes perfectly well.

Verified on `PD-KB401B` (HHKB Professional Classic, US layout), firmware A4.29,
macOS 27.0 on an M2.

## Status

| | |
|---|---|
| Read keyboard info, DIP state, mode | works |
| Read keymaps (all modes, both layers) | works |
| Write keymaps | works, takes effect immediately |
| Dump firmware | works, but freezes key input until replug |
| Flash firmware | not implemented |

## Build

```
make
```

Requires only the Xcode command line tools.

## Development

`compile_flags.txt` gives clangd what it needs; it resolves the macOS SDK on its
own, so no absolute paths are baked in. `.clang-format` describes the Linux
kernel style the sources use, and clangd applies it directly — the standalone
`clang-format` binary is not required.

`.zed/` carries folder settings and tasks for [Zed](https://zed.dev). The tasks
cover building, probing and dumping. Writing a keymap and dumping firmware are
deliberately absent: both have consequences you should not be one keystroke away
from.

To check a single file without building:

```
clangd --check=hhkb_probe.c
```

## Tools

### `hhkb_probe`

Read-only. Prints device info, DIP switch state, keyboard mode and both keymap
layers for the active mode.

### `hhkb_dump [dir]`

Dumps every stored keymap (3 modes x 2 layers) to `dir` (default `dumps/`) as
128-byte binaries plus a `manifest.json`. Run this before changing anything.

### `hhkb_write <mode> <fn> <key> <code> [--test] [--notify]`

Writes a single key. `mode` is 0=HHK, 1=Mac, 2=Lite; `fn` is 0 for the base
layer and 1 for the Fn layer; `key` is 1..60 (1 is the bottom-right key, 60 is
Esc); `code` is a USB HID keyboard usage.

The change is applied and left in place. `--test` applies it, verifies it, then
restores the previous value. A failed write always rolls back.

```
./hhkb_write 1 1 33 0x68      # Mac mode, Fn layer, the ] key -> F13
./hhkb_write 1 1 33 0x30      # put it back
```

Every write is bracketed by a read: the current map is fetched, modified,
written, and read back. **The per-chunk status byte is not a reliable success
signal** — only the read-back is.

### `hhkb_fwdump [out]`

Dumps the running application firmware (64 KiB) to `out` (default
`firmware.bin`).

### `decode_keymap.py [dir]`

Renders dumped keymaps as physical layouts and diffs them against HHK mode.

## Warnings

**`hhkb_fwdump` stops the keyboard from reporting key presses.** The vendor
channel keeps working and nothing stored on the board is modified, but key input
does not come back until you unplug and replug the keyboard. Have another input
device available before running it.

**Keymap editing is not a supported use of Classic models.** PFU's tool does not
offer it. This may matter for warranty purposes. Dump your keymaps first — a
factory reset path exists, but your own dump is the reliable way back.

**Do not touch the `UPDATEBOOT_*` commands** (`0xE4`–`0xE7`). They rewrite the
backup firmware bank, which is what recovers the board if the primary image is
damaged. They are deliberately not implemented here.

## Protocol

The keyboard exposes three USB HID interfaces. The third one — vendor usage page
`0xFF00`, 64-byte in and out reports — is the control channel.

Requests and responses are always 64 bytes:

```
request   AA AA <cmd> <chunk> <len> <payload...>
response  55 55 <cmd> <status> <chunk> <len> <payload...>
```

Note the asymmetry: the request payload starts at offset 5, the response payload
at offset 6, because the status byte only exists on the response.

Match the device by vendor ID `0x04FE` and primary usage page `0xFF00`. Send with
`IOHIDDeviceSetReport`; responses arrive as input reports, so register an input
report callback and run the run loop rather than calling `GetReport`. This
interface does not require Input Monitoring permission — the keyboard interfaces
do, but the vendor one does not.

## Credits

The wire protocol was originally reverse engineered by
[happy-hacking-gnu](https://gitlab.com/dom/happy-hacking-gnu) (The Unlicense),
a Linux implementation built on hidapi. This project reimplements it on IOKit
and verifies it against a Classic, which happy-hacking-gnu does not cover.

## Disclaimer

Use at your own risk. This is unofficial software with no connection to PFU.
