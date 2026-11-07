# OpenCore and multi-boot

The laptop has two NVMe drives: macOS on one, Windows and Ubuntu on the other (Ubuntu's shim
lives on the Windows EFI partition). OpenCore is on the macOS drive's EFI partition.

## Boot menu

- `Misc > Boot > PickerMode Builtin` – text picker, appears immediately (the graphical OpenCanopy
  picker drew slowly at this resolution).
- `Timeout 10`, default macOS: set the macOS volume as boot volume once with
  `sudo bless --mount / --setBoot`.
- `HideAuxiliary true`, `Misc > Tools` entries `Auxiliary true`: only macOS, Windows and Linux
  are visible; press **Space** in the picker to show tools, Reset NVRAM and recovery.
- Names: a `.contentDetails` file with the text `Windows` in `\EFI\Microsoft\Boot` on the Windows
  ESP, and `macOS` in the Preboot volume's `System/Library/CoreServices`.
- `Misc > Debug > Target 0`, `AppleDebug false`: no debug text and no log files on the ESP.

## Ubuntu entry

Ubuntu's `shimx64.efi` is on the Windows ESP, which OpenCore does not list by itself next to
Windows. Add a custom entry:

```xml
<key>Name</key><string>Linux</string>
<key>Flavour</key><string>Linux:Ubuntu</string>
<key>Path</key><string>PciRoot(0x0)/…/NVMe(0x1,…)/HD(1,GPT,…)/\EFI\ubuntu\shimx64.efi</string>
```

Get the exact device path from an OpenCore debug log (temporarily set `Misc > Debug > Target 67`,
boot once, search the log for `shimx64`).

## Performance profile at boot

Windows starts in the Quiet profile. `ASUSG835WMI.kext` sets Quiet (`DEVS 0x00120075 = 2`) when
it starts, so macOS does the same.
