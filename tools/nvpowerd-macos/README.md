# nvpowerd-macos – 150/175 W GPU power limits on macOS

A small macOS replacement for NVIDIA's Linux daemon `nvidia-powerd`, written for this laptop only
(ASUS ROG Strix SCAR 18 G835LXG, RTX 5090 Laptop `10de:2c58`, subsystem `1043:3b29`) and the
NullMoth driver stack (NVRM/NVAccel 2.8, open RM / GSP 620.70.06).

Without it, the GPU stays at its 95 W firmware base in every ASUS profile. With it, the
limit follows the profile (Fn+M4), as on Windows and on Linux with `nvidia-powerd`:

| ASUS profile | GPU limit | Sustained FP32 load (`sustained.swift`) |
| --- | --- | --- |
| Quiet | 95 W | ~1115 MHz (unchanged) |
| Balanced | 150 W | ~1115 → **~1450 MHz** (+30 %) |
| Turbo | 175 W (Dynamic Boost, CPU feedback) | ~1120 → **~1560 MHz** (+40 %) |

World of Warcraft does **not** get faster (39.6 / 40.2 / 40.8 FPS in Quiet / Balanced / Turbo):
on macOS it is not limited by GPU power but by the Metal→Vulkan/NVK path. GPU-bound work does
benefit.

## How it works

`nvidia-powerd` reads the platform's NVPCF tables through RM control `0x13b` and writes the GPU
limit with a PMGR control. This tool does the same through NullMoth's IOKit user client
(NVAccel/NVRM selector 1 = RM escape):

1. `NV0000_CTRL_CMD_SYSTEM_NVPCF_GET_POWER_MODE_INFO` (0x13b): supported functions, static
   controller config (version 37, 100 ms, EWMA 232, gains 3686/4096) and the dynamic profile
   tuple (cTGP offset, TPP target, boost range, AC enable). Needs the NVPCF `_DSM` path, i.e.
   [nvpcf-bridge](../../nullmoth-fix/nvpcf-bridge) loaded.
2. PMGR policy `0x2080a630` and policy info `0x2080a618`: checks floor/base/max/reference
   (5/95/175/150 W) before anything is written.
3. PMGR limit setter `0x2080e634` (privileged): 95 000, 150 000 or 175 000 mW.
4. Turbo only: a CPU feedback controller (as in the original) keeps CPU + GPU inside the 270 W
   platform budget. CPU package power comes from `powermetrics --samplers cpu_power`.

The service re-reads everything every 100 ms, follows profile changes and returns to 95 W on
exit, sleep, battery, missing CPU data or any unexpected value. There is no `--force` option.

### Driver 620 layout changes (found on this machine)

The original analysis was done against the Linux capture with RM 595.99.02. NullMoth 2.8 uses
RM/GSP 620.70.06, where four structures changed:

| Control | 595 | 620 | Change |
| --- | --- | --- | --- |
| `0x13b` NVPCF | 884 B | 896 B | `nvpcfFunc` added at +16, two DC-TSP fields removed (public headers) |
| `0x2080a630` policy | 1160 B | 2340 B | board-object mask E32 → E255, 64 entries, entries from +36 |
| `0x2080a618` policy info | 8632 B | 20512 B | selector at +0x98, entry `i` at 116 + 260·i, reference at +0x10c |
| `0x2080e634` setter | 392 B | 808 B | mask at +4, entries at +0x28 + 12·i: limit +0x2c, action byte +0x30 = `0xf7` |

The PMGR sizes come from the control table inside `gsp_ga10x.bin` (620.70.06); the read
layouts from read-only dumps on this laptop; the setter layout from the GSP handler of
`0x2080e634` (an action byte other than `0xf7`/`0xf4` returns `NV_ERR_INVALID_ARGUMENT`).
Also: the NVPCF dynamic request (sub-function 3) must carry the static config as input – RM
compares the BIOS reply's table version with the request (`NV_ERR_INVALID_DATA` otherwise).
macOS `powermetrics` alternates between ~100 ms and ~200 ms samples, so samples up to 250 ms
(age 350 ms) are accepted; a running Turbo controller holds its last target for up to 1 s
of missing samples (never higher), then falls back to 95 W.

Checked again with NullMoth 2.8.1 (version bump only: identical kext UUIDs, RM 620.70.06, same GSP
tables): service off 1115 MHz (Balanced) → on 1454 MHz (Balanced) / 1563 MHz (Turbo).

Only `--rm-version 595.99.02` and `620.70.06` are accepted. After a NullMoth update the service
refuses to start (GPU stays at 95 W) until the layouts have been checked again.

## Requirements

- NullMoth 2.8 (RM 620.70.06), AC power.
- [NVPCFBridge](../../nullmoth-fix/nvpcf-bridge) injected by OpenCore after Lilu (it is **not** in the
  EFI of this repo by default; add `NVPCFBridge.kext` to `Kernel → Add`). Without it, control `0x13b`
  fails and the service refuses to start.
- [ASUSG835WMI](../../drivers/ASUSG835WMI) for the profiles (Fn+M4).

## Build, test, install

Intel Mac with Xcode command line tools:

```bash
make CC="xcrun clang" test all
sudo build/nvpowerd-macos --probe --rm-version 620.70.06      # read-only check, writes nothing
sudo build/nvpowerd-macos --dump-pmgr /tmp/pmgr --rm-version 620.70.06   # read-only layout dump
sudo build/nvpowerd-macos --serve --watts 175 --rm-version 620.70.06     # foreground, Ctrl-C = 95 W
```

As a launchd service (`/Library/LaunchDaemons/local.nvpowerd-macos.plist`, log
`/var/log/nvpowerd-macos.log`); `--watts 175` means "follow the profile up to 175 W":

```bash
sudo ./install.sh 620.70.06 175
sudo launchctl bootstrap system /Library/LaunchDaemons/local.nvpowerd-macos.plist
sudo ./uninstall.sh            # back to 95 W
```

Tests run against byte-exact captures of the original daemon (Ubuntu, RM 595) and dumps from
this laptop (RM 620); IOKit and CoreFoundation are API doubles there.

## Scope and caveats

- Experimental, one machine, AC only. Other laptops have other OEM tables; the checks will
  refuse to run there – do not remove them.
- No NVIDIA, Apple or NullMoth code is included. NVIDIA's public open-gpu-kernel-modules headers
  (595.99.02, 620.70.06) were used to verify the public structures. The behaviour of the
  closed daemon was taken from an ioctl capture of the unmodified daemon on this laptop.
- License: MIT ([LICENSE](LICENSE)).
