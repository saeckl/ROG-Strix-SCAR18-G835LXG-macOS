# macOS Tahoe on the ASUS ROG Strix SCAR 18 (G835LXG)

OpenCore EFI, drivers and notes for running **macOS 26 Tahoe** on the ASUS ROG Strix SCAR 18
**G835LXG** (2025, Intel Core Ultra 9 290HX Plus, RTX 5090 Laptop GPU). It includes the first
working internal **speaker** driver for the TAS2781 amplifiers on this laptop, the ASUS **Fn keys**
(performance profiles, keyboard backlight, Aura effects) and the boot setup next to Windows and Ubuntu.

> Deutsche Kurzfassung: [README.de.md](README.de.md)

This is a hobby project on one laptop. Everything here was tested on exactly one machine. Read
[What does not work](#what-does-not-work) and make a backup of your EFI first.

## Tested system

| | |
| --- | --- |
| Laptop | ASUS ROG Strix SCAR 18 G835LXG, BIOS G835LXG.306 |
| CPU | Intel Core Ultra 9 290HX Plus (Arrow Lake-HX), 24 threads |
| GPU | NVIDIA RTX 5090 Laptop (`10de:2c58`), MUX set to dGPU in the BIOS; Intel iGPU disabled |
| Audio | Intel HDA `8086:7f50`, Realtek ALC285 (`0x10433f10`), 2× TI TAS2781 at I²C0 `0x38`/`0x39` |
| Network | Intel BE200 Wi-Fi 7, Intel Bluetooth `8087:0036`, Realtek RTL8125 2.5 GbE |
| Input | ASUS N-KEY keyboard `0b05:19b6`, I²C HID touchpad `ASUF1209` |
| macOS | 26.7.2 (25G332) |
| OpenCore | 1.0.9 DEBUG |
| SMBIOS | MacBookPro16,2 |

## Status

| Function | Status | How |
| --- | --- | --- |
| Boot, multi-boot macOS / Windows / Ubuntu | **works** (tested) | OpenCore 1.0.9, text picker showing only macOS, Windows, Linux; 10 s timeout → macOS |
| NVIDIA RTX 5090, internal panel, Metal | works with limitations | [NullMoth NVIDIA driver](https://github.com/nullmoth/nvidia-macos-driver) 2.7.2 |
| Internal speakers (left + right, balance slider) | **works** | VoodooHDA + [TAS2781DirectI2C](drivers/TAS2781DirectI2C) (this repo) |
| Headphone jack | works; plugging in mutes the speakers | VoodooHDA + jack sense in TAS2781DirectI2C 0.5.2 |
| Volume, mute | works | macOS volume → DAC gain |
| Wi-Fi (BE200) | works | AirportItlwm (Tahoe build) + IO80211FamilyLegacy / IOSkywalkFamily |
| Bluetooth | works | IntelBluetoothFirmware + IntelBTPatcher + BlueToolFixup |
| Ethernet RTL8125 | works | RTL812xLucy |
| Touchpad | works | VoodooI2C + VoodooI2CHID + LegionI2CArrowLake |
| Battery, sensors | works | VirtualSMC + plugins |
| Fn+M4 performance profile (Quiet / Balanced / Turbo) | **works** | [ASUSG835WMI](drivers/ASUSG835WMI) + [asus-hotkeys](tools/asus-hotkeys) (this repo) |
| Boot in Quiet profile (like Windows) | works | ASUSG835WMI sets Quiet at start |
| Fn+F2 / Fn+F3 keyboard backlight | **works** | asus-hotkeys (N-KEY feature report) |
| Fn+F4 Aura effects | **works** | asus-hotkeys |
| Lighting app (colours, 12 effects, zones keyboard / light bar / logo) | **works** | [ROG Licht](tools/asus-hotkeys) menu bar app (this repo) |
| Fn+F7 / Fn+F8 display brightness | software dimming only | the NVIDIA driver exposes no backlight, see below |
| Sleep / wake | **does not work** – the laptop reboots on wake (firmware is Modern Standby only, FADT `LOW_POWER_S0_IDLE`), see below | sleep disabled with `pmset` |
| Internal microphone | works (tested) | VoodooHDA |
| Webcam (ASUS FHD) | works (FaceTime live, up to 1920×1080); Photo Booth takes photos but shows no live preview | native UVC |

## What does not work

- **Real display brightness.** The internal panel is driven by the RTX 5090 (MUX = dGPU). The
  NullMoth framebuffer does not register a backlight, `_BCM` of the panel is empty and ASUS-WMI
  has no panel brightness. Fn+F7/F8 therefore dim with a gamma curve (25–100 %); the backlight
  and power draw stay the same.
- **World of Warcraft** runs since NullMoth 2.8 without a workaround (2.7.2 needed one, see below) at
  about 43–46 FPS, far below Windows (112 FPS in Quiet): the GPU stays at ~1.1 GHz because NVPCF is not
  answered, and the Metal→Vulkan/NVK path costs about another factor 2. Details and a patch for NullMoth:
  [docs/display-nvidia.md](docs/display-nvidia.md), [nullmoth-fix/](nullmoth-fix).

- **Sleep (S3).** macOS enters sleep normally, but on wake the firmware does not resume and
  the laptop boots from scratch (`Failure during sleep: 0x1F – EFI/Bootrom Failure after last
  point of entry to sleep`); open work is lost. The machine most likely supports only Windows'
  Modern Standby (S0ix). Workaround used here: no system sleep, only display sleep:
  ```bash
  sudo pmset -a sleep 0 standby 0 hibernatemode 0 powernap 0 disablesleep 1
  ```
  The laptop keeps running with the lid closed, so shut it down before putting it in a bag.
  Hibernation (OpenCore `HibernateMode`) has not been tried yet.

## Repository layout

| Path | Content |
| --- | --- |
| [`EFI/`](EFI) | OpenCore EFI as used on the test machine, **without** serials and without files that may not be redistributed (see [EFI/README.md](EFI/README.md)) |
| [`drivers/TAS2781DirectI2C/`](drivers/TAS2781DirectI2C) | Speaker amplifier driver (source, table generator, build script) |
| [`drivers/ASUSG835WMI/`](drivers/ASUSG835WMI) | ASUS-WMI kext for the performance profiles |
| [`audio/VoodooHDA/`](audio/VoodooHDA) | Build script for the VoodooHDA used here (WTLAudio based, with the SCAR edits) |
| [`tools/asus-hotkeys/`](tools/asus-hotkeys) | Fn-key agent and the **ROG Licht** menu bar app (lighting, zones, performance profile) |
| [`tools/diagnostics/`](tools/diagnostics) | HID logger, Aura report sender, audio device helper, ACPI dumper |
| [`docs/`](docs) | Findings: [audio](docs/audio-tas2781.md), [ASUS keys](docs/asus-hotkeys.md), [display/NVIDIA](docs/display-nvidia.md), [OpenCore/boot](docs/opencore.md), [hardware](docs/hardware.md) |
| [`install-kexts.sh`](install-kexts.sh) | Installs kexts to `/Library/Extensions` and rebuilds the Auxiliary Kernel Collection with backup and check |

## Installation (short)

Prerequisites: a working OpenCore/macOS Tahoe install on this laptop, SIP configured as in the
EFI (`csr-active-config 0x0A43`), Xcode Command Line Tools, and the NullMoth driver installed with
its manager app **1401**.

1. **EFI**: copy [`EFI/`](EFI), add the files listed in [EFI/README.md](EFI/README.md), generate
   your own SMBIOS (serial, MLB, ROM, UUID) with GenSMBIOS, and adjust the Ubuntu entry.
2. **NVIDIA**: install the NullMoth driver with 1401 (download from NullMoth, see links below).
3. **VoodooHDA**: `audio/VoodooHDA/build.sh` → `out/VoodooHDA.kext`.
4. **Speakers**: put the TI firmware into `drivers/TAS2781DirectI2C/firmware/`, then run
   `drivers/TAS2781DirectI2C/build.sh` **on the laptop** (it reads your factory calibration).
5. **ASUS profiles**: `drivers/ASUSG835WMI/build.sh`.
6. Install all three:
   ```bash
   sudo ./install-kexts.sh audio/VoodooHDA/out/VoodooHDA.kext drivers/TAS2781DirectI2C/build/TAS2781DirectI2C.kext drivers/ASUSG835WMI/build/ASUSG835WMI.kext
   ```
   Approve in System Settings → Privacy & Security, restart.
7. **Fn keys and ROG Licht**: `tools/asus-hotkeys/install.sh`, allow Input Monitoring.

Details for each step are in the README of each folder and in [`docs/`](docs).

## How the speakers were made to work (summary)

Full write-up: [docs/audio-tas2781.md](docs/audio-tas2781.md).

1. **The amplifiers read back all zeros until a software reset.** Earlier attempts thought the
   I²C bus or VoodooI2C was broken. In fact, after macOS boots, both TAS2781 ACK every transfer
   but return `0x00` for every register until `R01 := 0x01` (software reset), exactly what Linux
   does in `tasdevice_reset()`.
2. **A standalone polled I²C driver.** TAS2781DirectI2C talks to the DesignWare controller
   (`8086:7f4c`, `\_SB.PC02.I2C0`) directly, polled with interrupts masked, so it does not
   depend on VoodooI2C and can live in `/Library/Extensions`.
3. **Same init as Linux, byte for byte:** reset → DSP program 0 → configuration 0 → factory
   calibration from UEFI `CALI_DATA` → power-up on stream start (`R5C := D9`, `R02 := 00`) and
   shutdown on stop (`R02 := 02`). All 318 writes per amplifier were compared with a Linux trace.
4. **Both ALC285 paths are needed.** Linux drives DAC `0x02` → pin `0x14` *and* DAC `0x03` →
   pin `0x17` from the same stream. VoodooHDA creates two separate devices and gives DAC 0x03 no
   stream. The fix: hide pin `0x17` in VoodooHDA (one speaker device) and let the driver mirror
   format, stream and gain of DAC 0x02 onto DAC 0x03 / pin 0x17 via the HDA immediate command
   interface.

## Thanks

- **[NullMoth](https://github.com/nullmoth/nvidia-macos-driver)** – the NVIDIA driver that makes
  an RTX 50 laptop usable on macOS at all, and the 1401 manager app.
  Thank you for this remarkable work.
- **[Acidanthera](https://github.com/acidanthera)** – OpenCore, Lilu, VirtualSMC, WhateverGreen,
  RestrictEvents, NVMeFix, BrightnessKeys, BlueToolFixup, CpuTscSync.
- **[CloverHackyColor/VoodooHDA](https://github.com/CloverHackyColor/VoodooHDA)** and everyone
  who kept VoodooHDA alive.
- **watchthelight** – WTLAudio (VoodooHDA build for Arrow Lake-HX on Tahoe), LegionI2CArrowLake,
  LegionCpuPower. The VoodooHDA build here is based on WTLAudio.
- **[VoodooI2C](https://github.com/VoodooI2C/VoodooI2C)** – touchpad.
- **[OpenIntelWireless](https://github.com/OpenIntelWireless)** – IntelBluetoothFirmware,
  IntelBTPatcher, itlwm; **[kgp-macPro/AirportItlwm-Tahoe](https://github.com/kgp-macPro/AirportItlwm-Tahoe)**
  for the Tahoe Wi-Fi build.
- **[Dortania / OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher)**,
  **[dhinakg](https://github.com/dhinakg)** (USBToolBox, AMFIPass), **b00t0x** (CpuTopologyRebuild),
  the RTL812xLucy authors (InsanelyMac).
- **The Linux kernel** – `snd-soc-tas2781` / `tas2781-hda` (Texas Instruments, Shenghao Ding,
  Kevin Lu, Baojun Xu), `asus-wmi`, `hid-asus` – and **[asusctl](https://gitlab.com/asus-linux/asusctl)**.
  Without their open source this would not have been possible.
- **Texas Instruments** for publishing the TAS2781 firmware in linux-firmware.

Third-party components and their licences: [THIRD-PARTY.md](THIRD-PARTY.md).

## Licence

Own code in `drivers/`, `tools/`, `common/` and `install-kexts.sh`: GPL-2.0, see [LICENSE](LICENSE)
(the TAS2781 and ASUS-WMI drivers follow the Linux drivers they were checked against).
Third-party files keep their own licences.

No affiliation with ASUS, NVIDIA, Apple, Texas Instruments or NullMoth. Use at your own risk.
