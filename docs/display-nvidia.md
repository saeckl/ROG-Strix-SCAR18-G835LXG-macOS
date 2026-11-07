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
- **World of Warcraft (Metal): black screen.** `gx.log` shows
  `newComputePipelineState: vkCreateComputePipelines FAILED` (MTLLibraryErrorDomain Code=3,
  Prism shader), then `Device context was lost` in a loop (523× in one run) and `newBuffer Failed`.
  Reported to NullMoth.
