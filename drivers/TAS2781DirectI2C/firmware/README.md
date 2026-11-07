# TAS2781 firmware (not included)

Put these two files here before running `build.sh`:

| File | SHA-256 |
| --- | --- |
| `TAS2XXX3F100.bin` | `b7c8081289b6f868e5bdf328c008535102abf7f73e98f6d044d94148d287304f` |
| `TIAS2781RCA2.bin` | `71642c16668bb5235b4ddc84492bd8f29b7fb5401ef1f3ed33455f633c69505e` |

Both come from Texas Instruments and ship in [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git)
under `ti/audio/tas2781/`. On Ubuntu they are in `/lib/firmware/ti/audio/tas2781/` as `.bin.zst`:

```bash
zstd -d /lib/firmware/ti/audio/tas2781/TAS2XXX3F100.bin.zst -o TAS2XXX3F100.bin
zstd -d /lib/firmware/ti/audio/tas2781/TIAS2781RCA2.bin.zst -o TIAS2781RCA2.bin
```

(`/lib/firmware/TXNW2781RCA2.bin.zst` is a symlink to the second file.)

The file name is `TAS2XXX` + the low 16 bits of the ALC285 subsystem ID (`0x10433F10` → `3F10`)
+ the speaker ID (`0` on the tested G835LXG). The generator refuses any other file.
They are not in this repository because they are TI's files under TI's licence.
