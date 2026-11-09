# EFI (OpenCore 1.0.9)

The EFI used on the test machine, with personal data removed. **It does not boot as is.**

## Before use

1. **SMBIOS.** `PlatformInfo > Generic` contains placeholders (`SystemSerialNumber`, `MLB`,
   `SystemUUID`, `ROM`). Generate your own for `MacBookPro16,2` with
   [GenSMBIOS](https://github.com/corpnewt/GenSMBIOS). `ROM` can be the MAC address of the
   Ethernet port.
2. **Add the files that are not in this repository** (Apple binaries and NullMoth files may not
   be redistributed here):

   | File | Where it goes | Where to get it |
   | --- | --- | --- |
   | `IO80211FamilyLegacy.kext`, `IOSkywalkFamily.kext` | `OC/Kexts/` | [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) payloads, as described in [AirportItlwm-Tahoe](https://github.com/kgp-macPro/AirportItlwm-Tahoe) |
   | `HfsPlus.efi` | `OC/Drivers/` | [acidanthera/OcBinaryData](https://github.com/acidanthera/OcBinaryData) |
   | `apfs_aligned.efi` | `OC/Drivers/` | optional: Apple's APFS driver; alternatively remove the entry, OpenCore loads APFS itself (`UEFI > APFS`) |
   | `NullMothSafe.efi` | `OC/Tools/` | installed by NullMoth's **1401** app together with the driver |

   OpenCore logs an error and continues if one of these is missing, but Wi-Fi will not work
   without the two kexts.
3. **Ubuntu entry.** `Misc > Entries > Linux` is disabled and has a placeholder path. Take the
   real device path of `\EFI\ubuntu\shimx64.efi` from your OpenCore log (search for `shimx64`)
   and set `Enabled` to true. See [docs/opencore.md](../docs/opencore.md).

## Changes compared with a generic Arrow Lake EFI

- `Kernel > Block com.apple.iokit.IONDRVSupport` – otherwise the boot framebuffer takes index 0
  from NullMoth's NVRMFB (added by 1401).
- `DeviceProperties PciRoot(0x0)/Pci(0x2,0x0) disable-gpu` – Intel iGPU off (MUX = dGPU).
- Boot-args `nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth` (NullMoth), `nvrmacpibl=1` (NullMoth panel
  backlight through the NBCI `_DSM`, opt-in; see [docs/display-nvidia.md](../docs/display-nvidia.md)), `-vi2c-force-polling`
  (touchpad), `-ibtcompatbeta`, `-amfipassbeta`, `revpatch=sbvmm`, `ctrsmt=full`.
- `csr-active-config 0x0A43` – needed for kexts in `/Library/Extensions` (VoodooHDA, TAS2781,
  ASUS-WMI, NullMoth).
- Wi-Fi BE200: AirportItlwm with IO80211FamilyLegacy / IOSkywalkFamily, `IOSkywalkFamily` and
  `AppleEthernetRL` blocked (Tahoe only, `MinKernel 25`).
- Boot menu: `PickerMode Builtin` (text, fast), `Timeout 10`, `HideAuxiliary true`, debug
  output off (`Misc > Debug > Target 0`). Only macOS, Windows and Linux are shown.
- VoodooI2C here is the upstream **2.9.1** release. The test machine still runs a diagnostic
  build with extra logging from the audio investigation; the speaker driver does not need it.

## Not in the EFI on purpose

The speaker, ASUS-WMI and VoodooHDA kexts live in `/Library/Extensions` (Auxiliary Kernel
Collection), not in OpenCore: VoodooHDA must link against Apple's IOAudioFamily, which only works
there, and the others are easier to update without touching the EFI.
