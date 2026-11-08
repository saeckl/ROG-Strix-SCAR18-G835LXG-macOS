#!/usr/bin/env python3
"""Render docs/img/benchmark-os-comparison.svg from the 8 Nov 2026 SCAR-Bench / Geekbench 7 runs.
Same laptop (G835LXG, Core Ultra 9 290HX Plus, 2x32 GB DDR5-6400, RTX 5090 Laptop), ASUS profile Turbo.
Values from LEITSTAND: SCAR-Bench-{macOS,Windows,Ubuntu} and SCAR-Geekbench-macOS."""
import os

OS = {  # light, dark (reference categorical slots 1-3)
    "macOS 26": ("#2a78d6", "#3987e5"),
    "Windows 11": ("#eb6834", "#d95926"),
    "Ubuntu 26.04": ("#1baf7a", "#199e70"),
}
DRIVER = {
    "macOS 26": "26.7.2 · NVIDIA driver NullMoth 2.8.1 (open RM 620.70.06) + nvpowerd-macos",
    "Windows 11": "Pro · NVIDIA driver 621.14",
    "Ubuntu 26.04": "26.04.1 · NVIDIA open kernel module 595.99.02 + nvidia-powerd",
}
SECTIONS = [
    ("CPU", "Geekbench 7 CPU – higher is better", [
        ("Single-Core", "", [("macOS 26", 2734, ""), ("Windows 11", 2822, ""), ("Ubuntu 26.04", 2957, "")]),
        ("Multi-Core", "", [("macOS 26", 25578, ""), ("Windows 11", 26731, ""), ("Ubuntu 26.04", 28192, "")]),
    ]),
    ("GPU", "Geekbench 7 GPU – higher is better (different APIs: Metal / Vulkan / OpenCL)", [
        ("GPU score", "", [
            ("macOS 26", 103525, "Metal, without nvpowerd-macos (95 W)"),
            ("macOS 26", 155608, "Metal, with nvpowerd-macos (175 W)"),
            ("Windows 11", 197490, "Vulkan"),
            ("Windows 11", 222487, "OpenCL"),
            ("Ubuntu 26.04", 216293, "Vulkan"),
            ("Ubuntu 26.04", 238671, "OpenCL"),
        ]),
    ]),
    ("RAM", "scarbench, 24 threads, 2 × 32 GB DDR5-6400", [
        ("Bandwidth, triad (GB/s) – higher is better", "GB/s", [("macOS 26", 67.2, ""), ("Windows 11", 66.3, ""), ("Ubuntu 26.04", 66.4, "")]),
        ("Bandwidth, copy (GB/s) – higher is better", "GB/s", [("macOS 26", 69.8, ""), ("Windows 11", 75.1, ""), ("Ubuntu 26.04", 89.6, "")]),
        ("Latency (ns) – lower is better", "ns", [("macOS 26", 111.4, ""), ("Windows 11", 118.0, ""), ("Ubuntu 26.04", 114.5, "")]),
    ]),
]

W, LEFT, RIGHT = 860, 300, 90
BAR, GAP, PAD = 18, 8, 16
PLOT = W - LEFT - RIGHT

def fmt(v, unit):
    if unit: return f"{v:.1f} {unit}" if v < 1000 else f"{v:,.0f}"
    return f"{v:,.0f}".replace(",", " ")

def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;")

out, y = [], 0
y += 28
out.append(f'<text class="title" x="0" y="{y}">ASUS ROG Strix SCAR 18 (G835LXG) – same laptop, three operating systems</text>')
y += 20
out.append(f'<text class="sub" x="0" y="{y}">Core Ultra 9 290HX Plus · RTX 5090 Laptop · ASUS profile Turbo · 8 Nov 2026 · one run each</text>')
y += 22
for name in OS:
    cls = "os" + str(list(OS).index(name))
    out.append(f'<rect class="{cls}" x="0" y="{y-10}" width="12" height="12" rx="2"/>')
    out.append(f'<text class="lab" x="18" y="{y}"><tspan font-weight="600">{name}</tspan> – {esc(DRIVER[name])}</text>')
    y += 20
y -= 8
for title, note, groups in SECTIONS:
    y += 30
    out.append(f'<text class="sec" x="0" y="{y}">{title}</text>')
    out.append(f'<text class="sub" x="{8 + 11*len(title)}" y="{y}">{esc(note)}</text>')
    y += 6
    out.append(f'<line class="rule" x1="0" x2="{W}" y1="{y}" y2="{y}"/>')
    for gname, unit, rows in groups:
        y += 22
        out.append(f'<text class="grp" x="0" y="{y}">{esc(gname)}</text>')
        y += 8
        vmax = max(v for _, v, _ in rows) * 1.0
        lower_better = "lower is better" in gname
        rows = sorted(rows, key=lambda r: r[1], reverse=not lower_better)  # best value first
        for osname, v, extra in rows:
            cls = "os" + str(list(OS).index(osname)) + (" weak" if "without" in extra else "")
            label = osname + (f" – {extra}" if extra else "")
            w = max(2, PLOT * v / vmax)
            ty = y + BAR - 5
            out.append(f'<g><title>{esc(label)}: {fmt(v, unit)}</title>'
                       f'<text class="lab" x="{LEFT-10}" y="{ty}" text-anchor="end">{esc(label)}</text>'
                       f'<rect class="{cls}" x="{LEFT}" y="{y}" width="{w:.1f}" height="{BAR}" rx="4"/>'
                       f'<text class="val" x="{LEFT + w + 6:.1f}" y="{ty}">{fmt(v, unit)}</text></g>')
            y += BAR + GAP
y += 16
out.append(f'<text class="sub" x="0" y="{y}">Sorted best first. Bars start at zero; each group has its own scale. GPU APIs differ, so compare within an API.</text>')
y += 16
out.append(f'<text class="sub" x="0" y="{y}">Sources: tools/diagnostics/scarbench (scarbench, Geekbench 7.1.0 public results linked in its README).</text>')
H = y + 12

css = """
.title{font:600 17px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#0b0b0b}
.sec{font:600 15px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#0b0b0b}
.grp{font:600 12.5px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#52514e}
.lab{font:12.5px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#0b0b0b}
.val{font:600 12.5px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#0b0b0b;font-variant-numeric:tabular-nums}
.sub{font:12px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#52514e}
.rule{stroke:#d9d8d3;stroke-width:1}
.bg{fill:#fcfcfb}.os0.weak{fill:#86b6ef}
""" + "".join(f".os{i}{{fill:{c[0]}}}" for i, c in enumerate(OS.values())) + """
@media (prefers-color-scheme: dark){
.title,.sec,.lab,.val{fill:#f0efec}.grp,.sub{fill:#c3c2b7}.rule{stroke:#3a3936}.bg{fill:#1a1a19}
""" + "".join(f".os{i}{{fill:{c[1]}}}" for i, c in enumerate(OS.values())) + ".os0.weak{fill:#1c5cab}}"

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-16 0 {W+32} {H}" width="{W+32}" height="{H}" '
       f'role="img" aria-label="Benchmark comparison macOS, Windows, Ubuntu on the SCAR 18">'
       f'<style>{css}</style><rect class="bg" x="-16" y="0" width="{W+32}" height="{H}" rx="8"/>'
       + "".join(out) + "</svg>\n")
root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
dst = os.path.join(root, "docs/img/benchmark-os-comparison.svg")
os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, "w").write(svg)
print(dst, H)
