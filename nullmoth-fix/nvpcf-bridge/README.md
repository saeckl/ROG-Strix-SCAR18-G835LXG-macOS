# NVPCFBridge

**Needed by [nvpowerd-macos](../../tools/nvpowerd-macos)** (8 Nov 2026): with the bridge loaded, RM control
`0x13b` (NVPCF) works on macOS and nvpowerd-macos sets 150/175 W per profile. On its own (below) the bridge
gives no gain – the limit is written by the user-space daemon, as on Linux.

Lilu plugin that routes `_nv_acpi_dsm_method` in `com.nullmoth.NVRM` (exported symbol): calls with
`acpiNvpcfDsmFunction` go to `\_SB.NPCF._DSM`, as `kernel-open/nvidia/nv-acpi.c` does on Linux; everything else
goes to NullMoth's original. Boot-args `-nvpcfoff`, `-nvpcfdbg`. Build: `build.sh` (needs Lilu 1.7.x SDK and
MacKernelSDK in `sdk/`), inject after Lilu in OpenCore.

## Result on the G835LXG (NullMoth 2.8, 8 Nov 2026)

- Loads and boots fine; the route is installed.
- No gain: the GPU stays at or below ~1.1 GHz in every profile, WoW ~40–46 FPS as without it.
- Forwarding alone is not enough. This firmware's NVPCF is a two-way protocol:
  - sub-function 2 returns the TGP offset (`ACBT`) and a **total platform power** budget (`ATPP`, 45 W default);
  - sub-function 7 is where the driver reports its rated TGP back (`AVRT`, 95 W on Windows);
  - every profile change sends `Notify(\_SB.NPCF, 0xC0)`, which must reach RM (`rm_acpi_nvpcf_notify`);
  - the platform-power controller needs CPU power telemetry, which NVIDIA's Windows platform services provide.
- Second test: booted directly in Turbo (ASUS kext 0.1.4, `nvram asus-start-profile=performance`) so RM reads
  the Turbo tables at GPU init (120 W platform budget, TGP offset) without needing the notify: still ~40 FPS.
- So the bridge alone is not a fix; the missing piece was the `nvidia-powerd` equivalent, now
  [nvpowerd-macos](../../tools/nvpowerd-macos).

Side finding: writing the ASUS-WMI TGP / Dynamic Boost IDs changes EC registers (`MTGP`, `MGDB`) that survive a
restart; set them back to 0 (`devs=00120098.0`, `devs=001200C0.0`) if you experiment.
