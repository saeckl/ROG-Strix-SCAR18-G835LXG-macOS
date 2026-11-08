# scarbench – CPU/RAM/GPU comparison macOS vs. Windows vs. Linux

`scarbench.c` is one portable C file (MIT) that measures the same things on every OS:
core clock (dependent register add chain, single thread and all threads), integer and FP64 throughput
(1 thread / all threads), RAM bandwidth (STREAM triad/copy, 3 × 512 MiB) and RAM latency (pointer chase in
1 GiB). `scarbench sustain 60` keeps all cores busy and prints the mean clock every 5 s.

```bash
clang -O2 -o scarbench scarbench.c -lpthread      # macOS (gcc on Linux, cl /O2 or MinGW on Windows)
./scarbench
./scarbench sustain 60
```

- `bench-macos.command`: scarbench + `powermetrics` + sustained GPU load per ASUS profile, results to LEITSTAND.
- `geekbench-macos.command`: Geekbench 7 CPU and GPU (Metal) in Turbo, GPU with and without
  [nvpowerd-macos](../../nvpowerd-macos). The free Geekbench uploads results publicly.

## Results on the G835LXG (macOS 26.7.2, NullMoth 2.8.1, 8 Nov 2026)

scarbench (all three ASUS profiles within ±2 %):

| Test | macOS |
| --- | --- |
| Clock, 1 thread | 5.4 GHz |
| Clock, 24 threads, 60 s | P-cores 5.34 GHz, E-cores 4.66 GHz (powermetrics: PL1 limit in Turbo) |
| Integer / FP64, 24 threads | 22.6 Gop/s / 215 GFLOPS (17–21× scaling) |
| RAM triad / copy | 66–68 / 70–74 GB/s (2 × 32 GB DDR5-6400) |
| RAM latency | 112 ns |

Geekbench 7.1.0, Turbo:

| Test | macOS | Windows laptops, same CPU/GPU (Geekbench Browser, Nov 2026) |
| --- | --- | --- |
| CPU single / multi | [2734 / 25 578](https://browser.geekbench.com/v7/cpu/833755) | 2580–2936 / 18 985–26 549 |
| GPU Metal, nvpowerd-macos off (95 W) | [103 525](https://browser.geekbench.com/v7/gpu/377118) | – |
| GPU Metal, nvpowerd-macos on (175 W) | [155 608](https://browser.geekbench.com/v7/gpu/377109) | Vulkan ~172 000–198 000, OpenCL ~187 000–262 000 |

The CPU runs at full speed on macOS (no loss from the missing P/E-core scheduling). The GPU reaches about
80 % of typical Windows Vulkan scores with the service, ~55 % without. Same-machine Windows/Ubuntu runs
will follow.
