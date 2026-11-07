# Third-party components

Binaries in `EFI/` are unmodified releases from their authors. Each keeps its own licence;
see the linked projects for the full text and source code.

| Component | Version | Author / source | Licence |
| --- | --- | --- | --- |
| OpenCore, OpenRuntime, ResetNvramEntry, OpenCanopy, Resources | 1.0.9 DEBUG | [acidanthera/OpenCorePkg](https://github.com/acidanthera/OpenCorePkg) | BSD-3-Clause |
| Lilu | 1.7.3 | [acidanthera/Lilu](https://github.com/acidanthera/Lilu) | BSD-3-Clause |
| VirtualSMC, SMCBatteryManager, SMCLightSensor, SMCProcessor, SMCSuperIO | 1.3.9 | [acidanthera/VirtualSMC](https://github.com/acidanthera/VirtualSMC) | BSD-3-Clause |
| WhateverGreen | 1.7.2 | [acidanthera/WhateverGreen](https://github.com/acidanthera/WhateverGreen) | BSD-3-Clause |
| RestrictEvents | 1.1.7 | [acidanthera/RestrictEvents](https://github.com/acidanthera/RestrictEvents) | BSD-3-Clause |
| NVMeFix | 1.1.4 | [acidanthera/NVMeFix](https://github.com/acidanthera/NVMeFix) | BSD-3-Clause |
| BrightnessKeys | – | [acidanthera/BrightnessKeys](https://github.com/acidanthera/BrightnessKeys) | BSD-3-Clause |
| BlueToolFixup | 2.7.3 | [acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) | BSD-3-Clause |
| CpuTscSync | 1.1.3 | [acidanthera/CpuTscSync](https://github.com/acidanthera/CpuTscSync) | BSD-3-Clause |
| CpuTopologyRebuild | 2.0.2 | [b00t0x/CpuTopologyRebuild](https://github.com/b00t0x/CpuTopologyRebuild) | see project |
| AMFIPass | 1.4.1 | dhinakg, distributed with [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) | see project |
| USBToolBox, UTBDefault | – | [USBToolBox](https://github.com/USBToolBox/kext) (dhinakg) | see project |
| AirportItlwm (Tahoe) | 2.0.0 | [kgp-macPro/AirportItlwm-Tahoe](https://github.com/kgp-macPro/AirportItlwm-Tahoe), based on [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm) | GPL-2.0 |
| IntelBluetoothFirmware, IntelBTPatcher | 2.5.0 | [OpenIntelWireless/IntelBluetoothFirmware](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) | GPL-2.0 |
| RTL812xLucy | 1.1.3 | RTL812x driver from InsanelyMac (Mieze's RTL8111/LucyRTL8125 line) | see project |
| VoodooI2C, VoodooI2CHID, VoodooGPIO, VoodooInput, VoodooI2CServices | 2.9.1 | [VoodooI2C/VoodooI2C](https://github.com/VoodooI2C/VoodooI2C) | GPL-2.0 (VoodooGPIO GPL-3.0) |
| LegionI2CArrowLake | 1.2.0 | watchthelight | see author |
| LegionCpuPower | 0.3.3 | watchthelight | see author |
| AppleMCEReporterDisabler | – | community (plist-only kext) | – |

## Built from source by the scripts here

| Component | Source | Licence |
| --- | --- | --- |
| VoodooHDA 3.7.0 | [CloverHackyColor/VoodooHDA](https://github.com/CloverHackyColor/VoodooHDA) at `beb415a`, cloned by `audio/VoodooHDA/build.sh` | BSD-2-Clause (`audio/VoodooHDA/LICENSE`) |
| `audio/VoodooHDA/build.sh`, `legion.patch`, `src/kmod_info.c` | WTLAudio by watchthelight, adapted | as WTLAudio |

## Not included (download yourself)

| Component | Why | Where |
| --- | --- | --- |
| NullMoth NVIDIA driver, 1401, NullMothSafe.efi | NullMoth licence (PolyForm Noncommercial) and NVIDIA firmware | [nullmoth/nvidia-macos-driver](https://github.com/nullmoth/nvidia-macos-driver) |
| IO80211FamilyLegacy, IOSkywalkFamily, HfsPlus.efi, apfs_aligned.efi | Apple binaries | see [EFI/README.md](EFI/README.md) |
| TI TAS2781 firmware (`TAS2XXX3F100.bin`, `TIAS2781RCA2.bin`) | TI licence | [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git), `ti/audio/tas2781/` |
