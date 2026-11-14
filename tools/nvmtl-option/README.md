# nvmtl-option

Turns one NullMoth Metal option (`NVMTL_*`) on or off in the user block of
`/Library/GPUBundles/nvmtl/nvrm610.conf`. That block survives driver updates; the installer puts it back.
The script keeps a backup of the file next to it. Restart the app (not the Mac) afterwards.

```bash
sudo ./nvmtl-option.sh NVMTL_NAK_NO_PREDLOAD on    # WoW: avoids "vkCreateComputePipelines FAILED"
sudo ./nvmtl-option.sh NVMTL_NAK_NO_PREDLOAD off
```

Options tried on the G835LXG with World of Warcraft (NullMoth 2.9 / 3.0):

| Option | Effect here |
| --- | --- |
| `NVMTL_SHARED_VRAM=1` | kept on |
| `NVMTL_NAK_NO_PREDLOAD=1` | compute pipelines build again; WoW still fails on the `air.interpolate_center_perspective` shaders (NullMoth bug) |
| `NVMTL_ASYNC_COMMIT=1` | fewer FPS (35 instead of ~40), more stutter – left off |
| `NVMTL_ASYNC_TEX_UPLOAD=1` | no change – left off |
