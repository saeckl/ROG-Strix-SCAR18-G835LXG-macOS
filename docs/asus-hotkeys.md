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
| Effect | `5D B3 00 <mode> R G B <speed EB> 00 00 …` then `5D B5`, `5D B4` |

Modes used: `00` static, `01` breathing, `02` colour cycle, `03` rainbow. They can be changed in
`config.json` without rebuilding.

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
