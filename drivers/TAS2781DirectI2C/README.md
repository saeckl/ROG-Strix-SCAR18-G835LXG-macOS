# TAS2781DirectI2C

Driver for the two TAS2781 speaker amplifiers of the ASUS ROG Strix SCAR 18 G835LXG on macOS
Tahoe. Background: [docs/audio-tas2781.md](../../docs/audio-tas2781.md).

Requires the VoodooHDA build from [audio/VoodooHDA](../../audio/VoodooHDA) (one speaker device,
pin 0x17 hidden).

## Build (on the laptop itself)

1. Copy `TAS2XXX3F100.bin` and `TIAS2781RCA2.bin` into [`firmware/`](firmware) (see its README).
2. `./build.sh`
   - `generate_test_tables.py` decodes the firmware, reads **this machine's** factory calibration
     from NVRAM (`CALI_DATA`), checks its CRC and register addresses, and writes
     `TAS2781TestTables.hpp`.
   - Output: `build/TAS2781DirectI2C.kext` and `build/direct-control`.

Do not share your generated `TAS2781TestTables.hpp` or built kext: it contains your calibration
and TI's firmware data. Other units have different calibration values.

## Install

```bash
sudo ../../install-kexts.sh build/TAS2781DirectI2C.kext
```

Approve in System Settings, restart. About 10 s after login the amplifiers are initialised;
they power up when sound plays.

## Check

```bash
sudo build/direct-control status
ioreg -r -c TAS2781DirectI2C -l | grep TAS2781Direct
```

Healthy while playing: `R7C=A0`, `R02=00`, `R03=E8` on both amplifiers.

Other commands: `reset`, `init`, `powerup`, `shutdown`, `auto=0|1`, `atten=N` (0.5 dB steps),
`dump=BB.PP`, `wr=CH.BB.PP.RR.VV`, `verb=HEX` (HDA verb), `codecdump`, `coefdump`.
