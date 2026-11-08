# Display and NVIDIA

- The internal panel (**NE180QA1-MM0**, 18") is wired to the
  **RTX 5090 Laptop GPU** when the BIOS MUX is set to dGPU. The Intel iGPU is disabled in the EFI
  (`disable-gpu`).
- macOS has no driver for RTX 50. The **[NullMoth NVIDIA driver](https://github.com/nullmoth/nvidia-macos-driver)**
  (tested 2.7.2) makes it work: NVIDIA's open kernel modules + firmware in the kernel, Metal
  translated to Vulkan (NVK, "NVMTL over NVK"). Installed and updated with NullMoth's app
  **1401**, which also adds the OpenCore entries it needs (`IONDRVSupport` block,
  `NullMothSafe.efi`, boot-args).
- 1401 → **Send logs to NullMoth** uploads diagnostics (names and serials removed). For a game
  problem also attach the game's own log on NullMoth's website.

## Known limits (2.7.2)

- **No backlight control.** NVRMFB does not register a backlight for the panel, the panel's
  `_BCM` (`\_SB.PC00.RP01.PXSX.EDP1`) is empty, ASUS-WMI has no panel brightness.
  Workaround: gamma dimming (see asus-hotkeys). Possible future options: backlight support in
  NullMoth, or hybrid mode via the Intel iGPU (not tested).
- **World of Warcraft (Metal): black screen – fixed with a workaround.** `gx.log` showed
  `newComputePipelineState: vkCreateComputePipelines FAILED` (Prism) and `Device context was lost`
  in a loop. Cause: NullMoth's NAK optimisation `nak_cg_predload` turns loads into `load_global_nv`
  before they are legalised, and the compiler panics (`size_B <= intrin.align()` /
  `Invalid memory load/store size`). Workaround – add to `/Library/GPUBundles/nvmtl/nvrm610.conf`
  (an environment variable does not work, the release driver drops inherited `NVMTL_*` variables):
  ```
  # --- my settings, kept on update ---
  NVMTL_NAK_NO_PREDLOAD=1
  # --- end my settings ---
  ```
  Analysis, reproducer and a source patch for NullMoth: [nullmoth-fix/](../nullmoth-fix).
  Performance afterwards: about 30 FPS at render scale 67 % (≈ 2560×1600), 39 FPS at 50 %, GPU at
  99 %; CPU, power profile and `NVMTL_SHARED_VRAM=1` make no difference, so the limit is the
  Metal→Vulkan/NVK path and the GPU clock (Windows: ~100 FPS with the same settings, even in "Quiet").
- **GPU clock:** measured ~950 MHz (single dependent FMA chain) and 14 TFLOPS under full load. Windows in
  "Quiet" runs WoW at ~1245 MHz and ~107 FPS, so the clock explains only ~25–30 % of the gap; most of it is
  the Metal→Vulkan/NVK path. The NullMoth kernel driver does not answer the firmware's NVPCF/GPS power
  interfaces, which is why the ASUS profile does not change the GPU. Details:
  [nullmoth-fix/GPU-CLOCK.md](../nullmoth-fix/GPU-CLOCK.md).
- **Panel:** native 3840×2400 up to 240 Hz; macOS uses HiDPI "looks like 1920×1200", so games list
  1920×1200 and the real resolution is set with their render scale.
- **Photo Booth shows no live preview** (photos are taken, FaceTime shows live video). The camera
  delivers a steady stream in all formats, so this is most likely Photo Booth's Core Image/Metal
  preview path on the NullMoth driver.
