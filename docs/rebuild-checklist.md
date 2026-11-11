# Rebuild checklist (for Claude and other AI assistants)

How to bring a **fresh macOS 26 install** on the ASUS ROG Strix SCAR 18 G835LXG to the same state as the
reference install. The list is ordered: work from top to bottom, check each step before you start the
next one, and do not skip the checks.

Reference state (11 Nov 2026): macOS 26.7.2 (25G332), OpenCore 1.0.9, SMBIOS MacBookPro16,2,
NullMoth NVIDIA driver 2.9 (open RM 620.70.06), OCLP-CustoMac 3.0.3 root patch "Modern Wireless".

## 0. Rules

- **The user types every password.** Prepare `sudo` commands, or a `.command` file the user double-clicks.
  Never type or store a password yourself, even if the user offers one.
- **Back up the EFI before every change** (whole `EFI` folder, not only `config.plist`), and keep the
  way back written down.
- **Do not let NullMoth's 1401 app rebuild the EFI.** Installing or updating the driver with 1401 is fine;
  generating a new EFI with it overwrites the Wi-Fi, NVPCF and backlight settings below.
- Do not set the boot-arg `nvrmtgp=1`. Leave the GPU power floor at 95 W (nvpowerd-macos only raises it).
- Ask before pushing to GitHub or posting anything.
- Downloads from outside Apple and NullMoth (OCLP-CustoMac, USBToolBox, Wine and so on): the user downloads and starts them.

### Copy from the old install where you can

If the old macOS disk (Samsung 970 PRO 512 GB) is still installed, it is the known-good state. Copying
from it is faster and safer than building again, and the speaker driver contains this laptop's factory
calibration, so a copy is identical to a new build. Mount its data volume (`System - Daten`) and its EFI partition, then
copy:

| What | Old path |
| --- | --- |
| OpenCore EFI (with the real SMBIOS) | `EFI/` on the old disk's EFI partition |
| Speaker, audio, ASUS kexts | `/Library/Extensions/{VoodooHDA,TAS2781DirectI2C,ASUSG835WMI}.kext` |
| nvpowerd-macos binary + daemon | `/Library/PrivilegedHelperTools/nvpowerd-macos`, `/Library/LaunchDaemons/local.nvpowerd-macos.plist` |
| TI speaker firmware (if you must build) | `~/Documents/Codex/2026-11-06/referenced-chatgpt-conversation-this-is-an/work/tas2781/*.bin` |
| NullMoth settings block | `/Library/GPUBundles/nvmtl/nvrm610.conf` (block `# --- my settings, kept on update ---`) |

Kexts in `/Library/Extensions` must still be installed with `install-kexts.sh` (section 3). Copying the files there is not enough.

## 1. Where am I?

```bash
sw_vers                                    # expect 26.x
diskutil list                              # which disk holds this macOS
diskutil info / | grep "APFS Physical Store"   # e.g. disk1s2 -> its EFI partition is disk1s1
nvram 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:boot-path   # which EFI OpenCore was started from
csrutil status                             # must say "Custom Configuration"
```

Several disks can each have their own EFI partition. Find out which one OpenCore really starts from
(boot-path) before you change anything. Do not trust disk numbers from earlier sessions: they change when
disks are added or removed.

## 2. Order

| # | Step | Needs |
| --- | --- | --- |
| 1 | EFI as in section 4 | – |
| 2 | macOS update to 26.7.2 (Software Update) | Ethernet |
| 3 | Xcode Command Line Tools: `xcode-select --install`, then `sudo xcode-select -s /Library/Developer/CommandLineTools` | – |
| 4 | Wi-Fi root patch (OCLP-CustoMac) | Ethernet, EFI with AMFIPass + SIP 0x0A43 |
| 5 | NullMoth driver 2.9 **with DLSS** | 1401 or the 2.9 package |
| 6 | Own kexts: VoodooHDA, TAS2781DirectI2C, ASUSG835WMI | Command Line Tools |
| 7 | nvpowerd-macos | NullMoth installed, NVPCFBridge in EFI |
| 8 | asus-hotkeys + ROG Licht, clamshell-agent | ASUSG835WMI |
| 9 | Power settings (no sleep) | – |
| 10 | Final check (section 6) | – |

Restart after steps 1, 4, 5 and 6.

## 3. Install kexts into /Library/Extensions

```bash
sudo ./install-kexts.sh audio/VoodooHDA/out/VoodooHDA.kext \
  drivers/TAS2781DirectI2C/build/TAS2781DirectI2C.kext drivers/ASUSG835WMI/build/ASUSG835WMI.kext
```

The script keeps a backup of the Auxiliary Kernel Collection and stops if a kext that is loaded now
(including NullMoth's) would be missing afterwards. After it runs, approve the kexts in System Settings → Privacy & Security if
asked, then restart.

## 4. Device by device

### OpenCore / EFI (base for everything)

Start from [`EFI/`](../EFI) and [EFI/README.md](../EFI/README.md). Required state:

- **SMBIOS** MacBookPro16,2. Use the values from the old install's `config.plist` (`PlatformInfo > Generic`), not
  new ones, so the machine keeps its identity.
- **SIP** `csr-active-config` = `430A0000` (0x0A43); `SecureBootModel` Disabled.
- **Boot-args** (all of them):
  `debug=0x100 keepsyms=1 -vi2c-force-polling ctrsmt=full -ibtcompatbeta revpatch=sbvmm nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth amfi_get_out_of_my_way=0x1 amfi=0x80 ipc_control_port_options=0 gfxrst=4 -amfipassbeta nvrmacpibl=1`
- **Kernel > Add** in this order (Lilu first): Lilu, NVPCFBridge, AMFIPass, AppleMCEReporterDisabler, BlueToolFixup,
  BrightnessKeys, CpuTopologyRebuild, CpuTscSync, IntelBTPatcher, IntelBluetoothFirmware, LegionCpuPower,
  VoodooI2CServices, VoodooGPIO, VoodooI2C, NVMeFix, RTL812xLucy, RestrictEvents, VirtualSMC, SMCBatteryManager,
  SMCLightSensor, SMCProcessor, SMCSuperIO, USBToolBox, UTBDefault, VoodooInput, VoodooI2CHID, LegionI2CArrowLake,
  WhateverGreen, IOSkywalkFamily, IO80211FamilyLegacy, AirportItlwm (the last three with `MinKernel 25.0.0`, `MaxKernel 25.99.99`).
- **Kernel > Block**: `com.apple.iokit.IONDRVSupport` (all versions), `com.apple.iokit.IOSkywalkFamily` and
  `com.apple.driver.AppleEthernetRL` (both `MinKernel 25.0.0`).
- **DeviceProperties** `PciRoot(0x0)/Pci(0x2,0x0)` `disable-gpu` (iGPU off, BIOS MUX = dGPU).
- **Drivers**: OpenRuntime, OpenCanopy, ResetNvramEntry, HfsPlus, apfs_aligned (optional). **Tools**: NullMothSafe.efi
  (put there by 1401).

Check after the restart: `kextstat | grep -v com.apple` must list all EFI kexts above, plus the NullMoth kexts and our own kexts once they are installed.

### Wi-Fi – Intel BE200

Two parts. **Without part 2, Wi-Fi does not work**, even though AirportItlwm loads.

1. EFI: `AirportItlwm.kext` 2.0.0 (Tahoe build, [kgp-macPro/AirportItlwm-Tahoe](https://github.com/kgp-macPro/AirportItlwm-Tahoe)),
   `IO80211FamilyLegacy.kext` 1.0.0, `IOSkywalkFamily.kext` 1.2.0, plus the two blocks above.
2. Root patch with **OCLP-CustoMac 3.0.3** ([kgp-macPro/OCLP-CustoMac](https://github.com/kgp-macPro/OCLP-CustoMac),
   `OpenCore-Patcher.pkg`): open OpenCore-Patcher → Post-Install Root Patch → apply **Modern Wireless**, restart.
   It puts `IO80211.framework`, `WiFiPeerToPeer.framework` and `/usr/libexec/wifip2pd` (13.7.2) on the system volume.
   Needs Ethernet. The user downloads the pkg and runs it.

The installer and Recovery have **no Wi-Fi**: the root patch cannot exist there. Use an Ethernet cable.
After **every macOS update** the root patch must be applied again; OCLP's launch agent offers this after the update.

Check:
```bash
plutil -p /System/Library/CoreServices/OpenCore-Legacy-Patcher.plist | grep -A1 "Installed Patches"   # "Modern Wireless"
kextstat | grep AirportItlwm
networksetup -listallhardwareports | grep -A1 Wi-Fi
```

### Bluetooth – Intel 8087:0036

EFI only: IntelBluetoothFirmware, IntelBTPatcher, BlueToolFixup, boot-arg `-ibtcompatbeta`. Nothing to install.

### Ethernet – Realtek RTL8125

EFI only: RTL812xLucy. `AppleEthernetRL` is blocked. Works in the installer too.

### NVIDIA RTX 5090 Laptop – NullMoth driver

- Install **NullMoth 2.9** with the **1401** app (driver only, do not let it rebuild the EFI). The 1401 USB installer stick also carries
  `NullMoth/nullmoth-nvidia-2.9.tar.gz`.
- It installs `NVRM`, `NVRMFB`, `NVAccel`, `NVRMAGDC` and `NMXAudio` (HDMI audio) into `/Library/Extensions`, the Metal plugin into
  `/Library/GPUBundles/nvmtl/`, and `com.nullmoth.bootguard` / `com.nullmoth.recover` daemons.
- Settings file `/Library/GPUBundles/nvmtl/nvrm610.conf`, user block (survives updates):
  ```
  # --- my settings, kept on update ---
  NVMTL_SHARED_VRAM=1
  # --- end my settings ---
  ```
  Do **not** add `NVMTL_NAK_NO_PREDLOAD=1` (removed on purpose; it was only a workaround for the WoW black
  screen on 2.9).

Check:
```bash
cat /Library/NullMoth/driver-version          # 2.9
kextstat | grep nullmoth                      # NVRM, NVRMFB, NVAccel, NVRMAGDC, NMXAudio
system_profiler SPDisplaysDataType | grep -E "Chipset|Metal"
```

Known open issues with 2.9 (wait for NullMoth, do not work around): HDMI hot-plug (only a display connected
at boot works), WoW black screen / flicker, "Composited" instead of "Direct" in games.

### DLSS (MetalFX through NullMoth)

DLSS is an option of the NullMoth installer, not on by default. From the unpacked 2.9 package:

```bash
tar -xzf nullmoth-nvidia-2.9.tar.gz && cd pkgroot
sudo env NULLMOTH_DLSS=1 ./install.sh
```

The installer downloads ~600 MB from NVIDIA (driver 620.70.06, DLSS libraries, nvJitLink), checks SHA-256
and compiles for about 2 minutes. Restart afterwards.

Check `/Library/NullMoth/dlss/manifest.txt`: `dlss: 310.9.16`, `ngx: 620.70.06`, `nvjitlink: 12.9.86`,
`sm: 120`, `kernels: 259`. When a MetalFX app runs, DLSS writes logs to `$TMPDIR/nullmoth-dlss/`.

### Internal panel backlight (Fn+F7 / Fn+F8, brightness slider)

Boot-arg `nvrmacpibl=1` (NullMoth, opt-in) turns the panel into an `AppleBacklightDisplay` with 16 steps.
The keys come from asus-hotkeys. Check: `ioreg -r -c AppleBacklightDisplay | head -3`.
Details: [display-nvidia.md](display-nvidia.md).

### GPU power 95 / 150 / 175 W per ASUS profile

1. EFI: `NVPCFBridge.kext` right after Lilu (in [`EFI/`](../EFI); source in [nullmoth-fix/nvpcf-bridge](../nullmoth-fix/nvpcf-bridge)).
2. Daemon [tools/nvpowerd-macos](../tools/nvpowerd-macos):
   ```bash
   cd tools/nvpowerd-macos && make CC="xcrun clang" test all
   sudo build/nvpowerd-macos --probe --rm-version 620.70.06     # read-only check first
   sudo ./install.sh 620.70.06 175
   ```
   The daemon runs as `local.nvpowerd-macos` with `--serve --watts 175 --rm-version 620.70.06`.
   If NullMoth moves to a new RM version, check the nvpowerd README first, and do not just change the number.

Check: `sudo launchctl print system/local.nvpowerd-macos | grep state`. Under GPU load in Turbo the sustained
clock is ~1550 MHz (~1110 MHz without the daemon).

### Speakers – 2× TI TAS2781 + Realtek ALC285

- VoodooHDA: `audio/VoodooHDA/build.sh` → `audio/VoodooHDA/out/VoodooHDA.kext`.
- TAS2781DirectI2C 0.5.2: put `TAS2XXX3F100.bin` and `TIAS2781RCA2.bin` into
  `drivers/TAS2781DirectI2C/firmware/` (SHA-256 in its README), then run `drivers/TAS2781DirectI2C/build.sh` **on this laptop**
  (it reads the factory calibration from UEFI).
- Install both with `install-kexts.sh` (section 3).

Check: speakers left and right, balance slider, headphone jack mutes the speakers, volume keys.
Details: [audio-tas2781.md](audio-tas2781.md).

### ASUS performance profiles (Fn+M4: Quiet / Balanced / Turbo)

`drivers/ASUSG835WMI/build.sh`, install with `install-kexts.sh`. It boots in Quiet like Windows.

### Fn keys, keyboard lighting, ROG Licht

`tools/asus-hotkeys/install.sh` (as the user, no sudo). It installs LaunchAgents `de.codex.asushotkeys` and
`de.codex.roglicht` and the app `/Applications/ROG Licht.app`. Allow **Input Monitoring** for `asus-hotkeys`. After a
rebuild, remove the old entry with "−" and add it again with "+".
Details: [asus-hotkeys.md](asus-hotkeys.md).

### Lid closed with an external display

`tools/clamshell/install.sh` → LaunchAgent `as.saeckl.clamshell`. HDMI only works if the display is connected at boot.

### Touchpad, keyboard, battery, sensors, webcam, microphone

EFI only (VoodooI2C + VoodooI2CHID + LegionI2CArrowLake with `-vi2c-force-polling`, VirtualSMC + plugins).
The webcam and internal microphone work without extra drivers (microphone through VoodooHDA).

### USB

The EFI has only `UTBDefault` (no port map). On the USB-A ports, macOS therefore sees **USB 2.0 only**:
USB 3 sticks do not appear there, but they work on USB-C. To fix it, build a `UTBMap.kext` with
[USBToolBox](https://github.com/USBToolBox/tool) on Windows (plug a USB 2 and a USB 3 device into every port),
replace `UTBDefault` in the EFI with it, and keep `USBToolBox.kext`.

### Sleep

Sleep does not work: the laptop restarts on wake (the firmware supports only Modern Standby). It is turned off:

```bash
sudo pmset -a sleep 0 standby 0 hibernatemode 0 powernap 0 autopoweroff 0
sudo pmset -a disablesleep 1
sudo pmset -b displaysleep 5; sudo pmset -c displaysleep 10
```

## 5. Apps

- **World of Warcraft**: in `WTF/Config.wtf` set `SET accessibilityScreenNarrationEnabled "0"` (otherwise WoW reads
  the login screen aloud). Edit it only while WoW is closed.
- NullMoth **arm64 translation (nm-arm)**: needs the Command Line Tools selected (`xcode-select -s`). Universal apps
  such as Xcode do not need it.

## 6. Final check

```bash
sw_vers -productVersion
csrutil status
kextstat | grep -v com.apple | awk '{print $6, $7}'
cat /Library/NullMoth/driver-version; head -3 /Library/NullMoth/dlss/manifest.txt
plutil -p /System/Library/CoreServices/OpenCore-Legacy-Patcher.plist | grep -A1 "Installed Patches"
networksetup -listallhardwareports | grep -A1 Wi-Fi
sudo launchctl print system/local.nvpowerd-macos | grep state
launchctl list | grep -E "asushotkeys|roglicht|clamshell"
ioreg -r -c AppleBacklightDisplay | head -1
pmset -g | grep -E " sleep|hibernatemode|standby"
```

Expected loaded third-party kexts (reference install):
Lilu, NVPCFBridge, AMFIPass, BlueToolFixup, BrightnessKeys, CpuTscSync, IntelBTPatcher, LegionCpuPower,
VoodooI2CServices, NVMeFix, RestrictEvents, VirtualSMC, SMCBatteryManager, SMCLightSensor, WhateverGreen,
CpuTopologyRebuild, SMCProcessor, SMCSuperIO, USBToolBox, VoodooGPIO, VoodooI2C, VoodooI2CHID, RTL812xLucy,
AirportItlwm, VoodooInput, IntelBluetoothFirmware, NVRMFB, ASUSG835WMI, NVRM, NVAccel, NMXAudio, NVRMAGDC,
VoodooHDA, TAS2781DirectI2C.
