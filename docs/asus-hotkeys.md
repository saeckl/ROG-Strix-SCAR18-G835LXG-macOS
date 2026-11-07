# ASUS Fn keys, performance profiles and keyboard lighting

## Keys (N-KEY `0b05:19b6`, input report `0x5A`, usage page `0xFF31`)

Byte 1 of the report is the key code, `00` = released. One `EC` arrives at start without a key press.

| Key | Code | Action here |
| --- | --- | --- |
| Fn+M4 | `AE` | performance profile Quiet → Balanced → Turbo |
| Fn+F2 | `C5` | keyboard backlight down (0..3) |
| Fn+F3 | `C4` | keyboard backlight up |
| Fn+F4 | `B3` | next Aura effect |
| Fn+F7 | `10` | display darker (software) |
| Fn+F8 | `20` | display brighter (software) |

## Performance profiles (ASUS-WMI)

- `\_SB.ATKD` (`PNP0C14`, `_UID "ATK"`), method `WMNB(0, method, args)`.
  `DSTS = 0x53545344`, `DEVS = 0x53564544`.
- Thermal policy device ID `0x00120075` (Linux `ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY`). It is
  not in the DSDT; `WCHK` forwards unknown IDs to the BIOS via SMI, as on Linux.
- Values as in Linux: `2` Quiet, `0` Balanced, `1` Turbo. `DEVS` returns 1.
- The argument buffer must be **24 bytes**: `WMNB` creates fields up to offset 0x10; with 8 bytes
  the AML aborts.
- `DSTS` always returns `0x00010003` ("present"), not the current profile. The kext remembers the
  last value it set. At start it sets **Quiet**, like Windows.

`ASUSG835WMI.kext` accepts `profile=quiet|balanced|performance` and `profile?` from any user via
`IORegistryEntrySetCFProperties({"ASUSCommand": …})`; raw `dsts=` / `devs=` only from root.

## Keyboard backlight and Aura (N-KEY feature reports)

There is no WMI path (`DEVS 0x00050021` returns `0xFFFFFFFE`). Feature reports to the N-KEY:

| Purpose | Report |
| --- | --- |
| Handshake | `5A "ASUS Tech.Inc." 00` |
| Brightness 0..3 | `5A BA C5 C4 <level>` |
| Effect | `5D B3 <zone 00> <mode> R G B <speed> <dir> 00 R2 G2 B2 00 00 00 00` |
| Set / apply | `5D B5`, then `5D B4` |
| Zone power | `5D BD 01 b0 b1 b2 b3` (see below) |

Modes (from asusctl, all confirmed on the G835LXG): `0` static, `1` breathe (2 colours),
`2` colour cycle, `3` rainbow wave (direction), `4` stars (2 colours), `5` rain, `6` highlight,
`7` laser, `8` ripple, `10` pulse, `11` comet, `12` flash. Speed `E1` slow, `EB` medium,
`F5` fast. Direction `0` right, `1` left, `2` up, `3` down. There are no separate colour zones on
this model (asusctl `G835L`: per-key layout, `basic_zones: []`); one colour for all zones.

### Zone power (`5D BD 01`, 32-bit little endian)

| Zone | boot | awake | sleep | shutdown |
| --- | --- | --- | --- | --- |
| Logo | bit 0 | bit 2 | bit 4 | bit 6 |
| Keyboard | bit 1 | bit 3 | bit 5 | bit 7 |
| Light bar | bit 9 | bit 10 | bit 11 | bit 12 |

Examples tested: all on `5D BD 01 FF 1E 00 00`, keyboard only `5D BD 01 AA 00 00 00`,
keyboard + logo `FF 00 00 00`, keyboard + light bar `AA 1E 00 00`, light bar + logo `55 1E 00 00`.
The small display on the lid (ASUS AniMe/Slash) is a separate device and not handled here.

## ROG Licht (menu bar app)

Stand-in for Armoury Crate's lighting page: profile, effect, colours, speed, direction,
brightness and the three zones (awake, and optionally boot/sleep/shutdown). The app writes
`lighting.json`; the agent applies it on a distributed notification, so only the agent needs
Input Monitoring. Fn+F4 / Fn+F2 / Fn+F3 update the same state and the app follows.

## Display brightness

Not possible in hardware with the NullMoth driver, see [display-nvidia.md](display-nvidia.md).
The agent uses `CGSetDisplayTransferByFormula` on the built-in display (25–100 %) and re-applies it
after wake and display changes.

## The agent

`tools/asus-hotkeys` is a small Swift program run as a LaunchAgent. It needs **Input Monitoring**.
That permission is tied to the exact binary; after a rebuild remove the entry with "−" and add it
again with "+". Key mapping and Aura effects are in
`~/Library/Application Support/ASUSHotkeys/config.json` and are re-read on every key press.
Log: `~/Library/Logs/asus-hotkeys.log`.

## Tools

- `tools/diagnostics/hidlog.swift` – prints N-KEY input reports, to find key codes.
- `tools/diagnostics/aura.swift` – sends feature reports by hand.
- `drivers/ASUSG835WMI/wmictl.c` – sends commands to the WMI kext.
