# ASUSG835WMI

Small kext for the ASUS-WMI interface (`\_SB.ATKD`) of the G835LXG. Switches the firmware
performance profiles (Quiet / Balanced / Turbo) and sets **Quiet** at start.
Background: [docs/asus-hotkeys.md](../../docs/asus-hotkeys.md).

```bash
./build.sh
sudo ../../install-kexts.sh build/ASUSG835WMI.kext
```

Test (no root needed for profiles):

```bash
build/wmictl profile=balanced
build/wmictl 'profile?'
```

Start profile: Quiet by default. Override with an NVRAM variable (0.1.4):

```bash
sudo nvram asus-start-profile=performance   # quiet | balanced | performance | keep
sudo nvram -d asus-start-profile             # back to Quiet
```

Fn+M4 is handled by [tools/asus-hotkeys](../../tools/asus-hotkeys), which uses this kext.
