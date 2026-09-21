#!/usr/bin/env python3
"""Renders dumped HHKB keymaps as physical layouts and diffs them against HHK mode."""
import sys
import pathlib

NAMES = {
    0x00: "--", 0x01: "Fn",
    0x04: "A", 0x05: "B", 0x06: "C", 0x07: "D", 0x08: "E", 0x09: "F", 0x0A: "G",
    0x0B: "H", 0x0C: "I", 0x0D: "J", 0x0E: "K", 0x0F: "L", 0x10: "M", 0x11: "N",
    0x12: "O", 0x13: "P", 0x14: "Q", 0x15: "R", 0x16: "S", 0x17: "T", 0x18: "U",
    0x19: "V", 0x1A: "W", 0x1B: "X", 0x1C: "Y", 0x1D: "Z",
    0x1E: "1", 0x1F: "2", 0x20: "3", 0x21: "4", 0x22: "5", 0x23: "6", 0x24: "7",
    0x25: "8", 0x26: "9", 0x27: "0",
    0x28: "Enter", 0x29: "Esc", 0x2A: "BSpc", 0x2B: "Tab", 0x2C: "Space",
    0x2D: "-", 0x2E: "=", 0x2F: "[", 0x30: "]", 0x31: "\\", 0x33: ";", 0x34: "'",
    0x35: "`", 0x36: ",", 0x37: ".", 0x38: "/", 0x39: "Caps",
    0x3A: "F1", 0x3B: "F2", 0x3C: "F3", 0x3D: "F4", 0x3E: "F5", 0x3F: "F6",
    0x40: "F7", 0x41: "F8", 0x42: "F9", 0x43: "F10", 0x44: "F11", 0x45: "F12",
    0x46: "PrtSc", 0x47: "ScrLk", 0x48: "Pause", 0x49: "Ins", 0x4A: "Home",
    0x4B: "PgUp", 0x4C: "Del", 0x4D: "End", 0x4E: "PgDn", 0x4F: "Right",
    0x50: "Left", 0x51: "Down", 0x52: "Up", 0x53: "NumLk",
    0x54: "KP/", 0x55: "KP*", 0x56: "KP-", 0x57: "KP+", 0x58: "KPEnt",
    0x66: "Power", 0x78: "Stop",
    # International1-5: Ro, Kana, Yen, Henkan, Muhenkan.
    0x87: "Ro", 0x88: "Kana", 0x89: "Yen", 0x8A: "Henkn", 0x8B: "Muhen",
    0xE0: "LCtrl", 0xE1: "LShft", 0xE2: "LAlt", 0xE3: "LGUI", 0xE4: "RCtrl",
    0xE5: "RShft", 0xE6: "RAlt", 0xE7: "RGUI",
    # Reserved in the HID keyboard page; the firmware routes these out of the
    # consumer interface instead. Exact functions unconfirmed.
    0xE8: "Med-A", 0xE9: "Med-B", 0xEA: "Med-C", 0xEB: "Med-D",
}

# Key numbers run right-to-left, bottom-to-top: 1 is bottom-right, 60 is Esc.
ROWS = [
    [60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46],
    [45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32],
    [31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19],
    [18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6],
    [5, 4, 3, 2, 1],
]

MODES = [(0, "hhk"), (1, "mac"), (2, "lite")]


def name(code):
    return NAMES.get(code, "%02X" % code)


def render(layout):
    out = []
    for row in ROWS:
        out.append(" ".join(f"{name(layout[k]):>5}" for k in row))
    return "\n".join(out)


def main():
    d = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "dumps")
    maps = {}
    for mode, mname in MODES:
        for fn in (0, 1):
            p = d / f"keymap_{mode}_{mname}_fn{fn}.bin"
            if p.exists():
                maps[(mode, fn)] = p.read_bytes()

    for (mode, fn), layout in sorted(maps.items()):
        mname = dict(MODES)[mode]
        print(f"===== mode {mode} ({mname}) / {'Fn' if fn else 'Base'} layer =====")
        print(render(layout))
        print()

    print("===== differences vs mode 0 (hhk) =====")
    for (mode, fn), layout in sorted(maps.items()):
        if mode == 0:
            continue
        base = maps.get((0, fn))
        if not base:
            continue
        mname = dict(MODES)[mode]
        print(f"-- mode {mode} ({mname}) / {'Fn' if fn else 'Base'} --")
        for key in range(1, 61):
            if layout[key] != base[key]:
                print(f"   key {key:2d}: {name(base[key]):>6} ({base[key]:02X})"
                      f"  ->  {name(layout[key]):>6} ({layout[key]:02X})")
        print()


if __name__ == "__main__":
    main()
