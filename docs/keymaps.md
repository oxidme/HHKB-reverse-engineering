# Factory keymaps

Read off a `PD-KB401B` (HHKB Professional Classic, US layout) in factory
condition, firmware A4.29. Two runs on separate USB sessions produced identical
bytes, and a `RESET_FACTORY_DEFAULTS` afterwards reproduced them exactly.

Each layer is 128 bytes indexed by key number, keys 1 to 60, numbered from the
bottom right. See [protocol.md](protocol.md#keymaps) for the layout rules and
[protocol.md](protocol.md#keycodes) for the non-standard codes.

Reproduce these with `hhkb_dump`, and restore them with `hhkb_reset --yes`.

## What the modes differ in

The three modes share almost everything. Taking HHK as the baseline:

- **Mac and Win** replace Henkan and Muhenkan on the diamond keys with the GUI
  modifiers.
- **Mac** alone has the media keys, Power on Fn+Esc, and Backspace rather than
  Delete on the top right key.
- **Mac and Win** both have Caps Lock on Fn+Tab and the keypad codes on the Fn
  layer; HHK has neither.

Media keys, Power and Caps Lock being Mac-only matches the Fn key table in the
manual, which marks them supported in the Mac column only.

## mode 0 — HHK

### Base

```
00E68A2C8BE201E538373610110519061B1DE12834330F0E0D0B0A09071604E0
4C302F13120C181C1715081A142B35312E2D27262524232221201F1E29000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
  Esc     1     2     3     4     5     6     7     8     9     0     -     =     \     `
  Tab     Q     W     E     R     T     Y     U     I     O     P     [     ]   Del
LCtrl     A     S     D     F     G     H     J     K     L     ;     ' Enter
LShft     Z     X     C     V     B     N     M     ,     .     / RShft    Fn
 LAlt Muhen Space Henkn  RAlt
```

### Fn

```
00E6782C8BE201E5514E4D10110519061B1DE1284F504B4A0D0B0A09071604E0
2A3052484746181C1715081A142B4C494544434241403F3E3D3C3B3A29000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
  Esc    F1    F2    F3    F4    F5    F6    F7    F8    F9   F10   F11   F12   Ins   Del
  Tab     Q     W     E     R     T     Y     U PrtSc ScrLk Pause    Up     ]  BSpc
LCtrl     A     S     D     F     G     H     J  Home  PgUp  Left Right Enter
LShft     Z     X     C     V     B     N     M   End  PgDn  Down RShft    Fn
 LAlt Muhen Space  Stop  RAlt
```

## mode 1 — Mac

### Base

```
00E6E72CE3E201E538373610110519061B1DE12834330F0E0D0B0A09071604E0
2A302F13120C181C1715081A142B35312E2D27262524232221201F1E29000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
  Esc     1     2     3     4     5     6     7     8     9     0     -     =     \     `
  Tab     Q     W     E     R     T     Y     U     I     O     P     [     ]  BSpc
LCtrl     A     S     D     F     G     H     J     K     L     ;     ' Enter
LShft     Z     X     C     V     B     N     M     ,     .     / RShft    Fn
 LAlt  LGUI Space  RGUI  RAlt
```

### Fn

```
00E6E72CE3E201E5514E4D56570519061B1DE1584F504B4A54550AEBEAE9E8E0
533052484746181C1715081A14394C494544434241403F3E3D3C3B3A66000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
Power    F1    F2    F3    F4    F5    F6    F7    F8    F9   F10   F11   F12   Ins   Del
 Caps     Q     W     E     R     T     Y     U PrtSc ScrLk Pause    Up     ] NumLk
LCtrl  Vol-  Vol+  Mute Eject     G   KP*   KP/  Home  PgUp  Left Right KPEnt
LShft     Z     X     C     V     B   KP+   KP-   End  PgDn  Down RShft    Fn
 LAlt  LGUI Space  RGUI  RAlt
```

## mode 2 — Win

### Base

```
00E6E72CE3E201E538373610110519061B1DE12834330F0E0D0B0A09071604E0
4C302F13120C181C1715081A142B35312E2D27262524232221201F1E29000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
  Esc     1     2     3     4     5     6     7     8     9     0     -     =     \     `
  Tab     Q     W     E     R     T     Y     U     I     O     P     [     ]   Del
LCtrl     A     S     D     F     G     H     J     K     L     ;     ' Enter
LShft     Z     X     C     V     B     N     M     ,     .     / RShft    Fn
 LAlt  LGUI Space  RGUI  RAlt
```

### Fn

```
00E6E72CE3E201E5514E4D56570519061B1DE1584F504B4A54550A09071604E0
2A3052484746181C1715081A14394C494544434241403F3E3D3C3B3A29000000
0000000000000000000000000000000000000000000000000000000000000000
0000000000000000000000000000000000000000000000000000000000000000
```

```
  Esc    F1    F2    F3    F4    F5    F6    F7    F8    F9   F10   F11   F12   Ins   Del
 Caps     Q     W     E     R     T     Y     U PrtSc ScrLk Pause    Up     ]  BSpc
LCtrl     A     S     D     F     G   KP*   KP/  Home  PgUp  Left Right KPEnt
LShft     Z     X     C     V     B   KP+   KP-   End  PgDn  Down RShft    Fn
 LAlt  LGUI Space  RGUI  RAlt
```

## Differences in full

Key numbers, against mode 0.

### Mac, base — 3 bytes

| Key | HHK | Mac |
|---|---|---|
| 2 | `8A` Henkan | `E7` RGUI |
| 4 | `8B` Muhenkan | `E3` LGUI |
| 32 | `4C` Delete | `2A` Backspace |

### Mac, Fn — 14 bytes

| Key | HHK | Mac |
|---|---|---|
| 2 | `78` Stop | `E7` RGUI |
| 4 | `8B` Muhenkan | `E3` LGUI |
| 11 | `10` M | `56` keypad − |
| 12 | `11` N | `57` keypad + |
| 19 | `28` Enter | `58` keypad Enter |
| 24 | `0D` J | `54` keypad / |
| 25 | `0B` H | `55` keypad * |
| 27 | `09` F | `EB` Eject |
| 28 | `07` D | `EA` Mute |
| 29 | `16` S | `E9` Volume Up |
| 30 | `04` A | `E8` Volume Down |
| 32 | `2A` Backspace | `53` Num Lock |
| 45 | `2B` Tab | `39` Caps Lock |
| 60 | `29` Esc | `66` Power |

### Win, base — 2 bytes

| Key | HHK | Win |
|---|---|---|
| 2 | `8A` Henkan | `E7` RGUI |
| 4 | `8B` Muhenkan | `E3` LGUI |

### Win, Fn — 8 bytes

The Mac Fn differences minus the media keys (27–30), Num Lock (32) and Power
(60). The keypad codes and Caps Lock are present in Win too.

## Where an Fn layer shows a plain letter

Positions where the Fn layer repeats the base keycode are unassigned: pressing
Fn with that key just types the letter. That is the keyboard's documented
behaviour, not a gap in the dump.
