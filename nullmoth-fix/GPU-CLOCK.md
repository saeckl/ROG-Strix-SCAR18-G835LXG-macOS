# RTX 5090 Laptop: clock and throughput under NullMoth 2.7.2 vs Windows (ASUS G835LXG)

## Measurements (macOS 26.7.2, NullMoth 2.7.2, MUX = dGPU)

| Test | Result |
| --- | --- |
| `clock.swift`: one thread, dependent FMA chain (4-cycle latency assumed) | ~950 MHz (937–988) |
| `flops.swift`: 82×128×512 threads, 10 independent FMA chains | 14.1 TFLOPS FP32 ≈ 670 MHz effective (expected ~35–40 TFLOPS at boost) |
| World of Warcraft login screen, render scale 100 %, uncapped | 53 FPS, CPU blocked on GPU 98 % |
| WoW in game, render scale 50 % / 67 % | ~39 / ~30 FPS (Windows 11, same settings, ASUS "Quiet": ~100 FPS) |
| `NVMTL_NO_ORDER_BARRIER=1`, `NVMTL_NO_ROBUST=1`, `NVMTL_SHARED_VRAM=0/1` | no change (±2 %) |
| ASUS profile Quiet → Turbo (EC/WMI) | no change in FPS, only fan speed |

## Likely cause

The laptop firmware provides the NVIDIA platform power interfaces: `\_SB.NPCF` (`NVDA0820`, NVPCF _DSM
`36b49710-2483-11e7-9598-0800200c9a66`) and GPS on the GPU node (`a3132d01-8cda-49ba-a52e-bc9d46df6b81`).
`kexts/NVRM/os-xnu2.cpp` answers every _DSM except NBCI with "not supported" (`acpiNvpcfDsmFunction` →
`NV_ERR_NOT_SUPPORTED`), so RM never gets the configurable TGP / Dynamic Boost tables and the GPU appears to stay
near base clock and base power regardless of the platform profile. Forwarding the NVPCF and GPS _DSM calls the
way `kernel-open/nvidia/nv-acpi.c` does would likely restore boost clocks on MUX laptops.

## Windows reference (same laptop, ASUS "Quiet", NVIDIA 621.14)

GPU-Z: base 1095 MHz, boost 1597 MHz (spec). In World of Warcraft (2560×1440 window, render scale 98 %,
quality 7): GPU clock ~1245 MHz, GPU load 96 %, ~107 FPS.

So in "Quiet" Windows also runs near 1.2 GHz. The clock difference to macOS (~950 MHz measured with a light
load) is at most ~25–30 %, while the frame rate differs by ~3× (~107 vs ~30–39 FPS at a similar or lower
pixel count and lower quality). Most of the gap is therefore in the Metal→Vulkan/NVK path, not the clock.
The missing NVPCF/GPS handling still explains why the ASUS profile has no effect on the GPU.

## Profile comparison, 8 Nov 2026 (NullMoth 2.8 / NVIDIA 621.14 on Windows 11)

Windows (`tools/diagnostics/SCAR-Messung-Windows.ps1`, nvidia-smi + PresentMon, WoW same spot and settings):

| Profile | GPU power limit | GPU clock | Memory clock | Power draw | WoW FPS |
| --- | --- | --- | --- | --- | --- |
| Quiet | 95 W | 1409 MHz | 10.5 GHz | 94 W | 112 |
| Balanced | 150 W | 1858 MHz | 14.0 GHz | 148 W | 142 |
| Turbo | 175 W | 2035 MHz | 14.1 GHz | 171 W | 154 |

ASUS-WMI on Windows: base TGP (`0x00120099`) = 95, TGP offset (`0x00120098`) = 0 (Quiet) / 55 (Balanced, Turbo);
Turbo adds 25 W Dynamic Boost. On macOS both read 0: the firmware gets these values to the GPU through NVPCF.

macOS 2.8 (`sustained.swift`, 15 s FP32 load, profile switched through ASUS-WMI):

| Profile | Sustained | Approx. clock |
| --- | --- | --- |
| Quiet | 21.8 TFLOPS | ~1040–1110 MHz |
| Balanced | 21.6 TFLOPS | ~1030 MHz |
| Turbo | 23.0 TFLOPS | ~1030–1120 MHz |

WoW on macOS 2.8: ~46 FPS (user reading), any profile.

Conclusion: on macOS the GPU stays at roughly 1.0–1.1 GHz in every profile, below even Windows "Quiet"
(1.41 GHz at 95 W). Without NVPCF the profile cannot raise the GPU power limit. This explains a
1.3–1.9× clock gap; the remaining ~2× FPS gap is the Metal→Vulkan/NVK path.
