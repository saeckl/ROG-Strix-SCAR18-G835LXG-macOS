# VoodooHDA for the SCAR 18

Tahoe has no AppleHDA, so the ALC285 is driven by [VoodooHDA](https://github.com/CloverHackyColor/VoodooHDA).
`build.sh` comes from **WTLAudio** by watchthelight (VoodooHDA for Arrow Lake-HX on Tahoe) and:

- clones VoodooHDA at the audited commit `beb415a` and applies `legion.patch` (bounds checks in
  the mixer user client, a null check, LayoutId handling),
- builds the `LE` variant (links Apple's IOAudioFamily, lives in `/Library/Extensions`),
- matches only the HDA controller `8086:7f50`, `LayoutId 0` (keeps the ASUS pin configuration),
- **SCAR edits:** `NodesToPatch` pin `0x17` → `0x411111f0` (one speaker device instead of two)
  and PCM mixer 100. The second speaker path is driven by TAS2781DirectI2C.

```bash
./build.sh                 # -> out/VoodooHDA.kext
sudo ../../install-kexts.sh out/VoodooHDA.kext
```

The result is byte-identical to the kext running on the test machine
(sha256 of the binary `ff74f95f5e445a6c6e77df59842d53b3a20a1c8914357178c76c26a38c34888f`).
