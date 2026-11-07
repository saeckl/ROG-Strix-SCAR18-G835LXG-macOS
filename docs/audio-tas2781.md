# Internal speakers: ALC285 + 2× TAS2781

## Hardware

- Intel HDA controller `8086:7f50` (Arrow Lake-HX), codec **Realtek ALC285**, subsystem `0x10433f10`.
- Two **TI TAS2781** smart amplifiers on `\_SB.PC02.I2C0` (DesignWare controller `8086:7f4c`),
  addresses `0x38` (left) and `0x39` (right). ACPI device `TXNW2781`. `0x3A` NACKs.
- The ALC285 sends I²S to the amplifiers; the amplifiers are configured over I²C. Without the
  I²C setup the speakers stay silent even if VoodooHDA plays.
- DSP firmware `TAS2XXX3F100.bin` (`3F10` = subsystem low 16 bits, speaker ID `0` from GPIO
  `GPP_S_5`), power profile `TIAS2781RCA2.bin` (Ubuntu symlink `TXNW2781RCA2.bin`).
- Factory calibration in the UEFI variable
  `1f52d2a1-bb3a-457d-bc09-43a3f4310a92:CALI_DATA` (chip 2781, 3 records + CRC32).

## Finding 1 – all registers read 0x00 until a software reset

Long thought to be an I²C timing/VoodooI2C problem. Measured with a polled driver that talks to
the controller directly:

| | before reset | after reset | Linux |
| --- | --- | --- | --- |
| R7C (revision) 0x38/0x39 | 00 / 00 | A0 / A0 | A0 |
| R02 | 00 | 1A | 02 |
| R03 | 00 | 28 | E8 |

The amplifiers ACK every transfer but return zeros until `B0/P0/R01 := 0x01`. Linux does this
in `tasdevice_reset()` on every probe; earlier macOS attempts never did.

## Finding 2 – the init sequence

Identical to Linux (318 writes per amplifier, checked against a Linux I²C trace):

1. Software reset (`R01 := 01`).
2. DSP program 0 ("Tuning Mode") and configuration 0 (`G835_JGX…`) from `TAS2XXX3F100.bin`.
   Block types `0x01` go to both amplifiers, `0x02/12/22/41/45` to `0x38`, `0x03/13/23/42/46` to `0x39`.
3. Factory calibration: 5 values per amplifier from `CALI_DATA`, converted as Linux
   `cali_cnv()`; register addresses from the firmware trailer. If the nominal impedance is above
   the calibrated one, the sine gain is corrected (`tas2781_cali_preproc()`); not needed on the
   test unit.
4. Stream start (`PRE_POWER_UP` from the RCA file): `R5C := D9`, `R02 := 00`.
   Stream stop (`PRE_SHUTDOWN`): `R02 := 02`.
5. Healthy state while playing: `R7C = A0`, `R02 = 00`, `R03 = E8`, `R5C = D9`.

Book/page selection: `R00` = page, `R7F` = book (on page 0).

## Finding 3 – both codec paths are needed

| Pin | DAC | Linux | VoodooHDA default |
| --- | --- | --- | --- |
| `0x14` | `0x02` | stream 1, ch 0 | speaker device 1 |
| `0x17` | `0x03` | stream 1, ch 0 (same stream) | separate device 2, no stream |

With only one path you get thin sound, wrong balance, or silence after a system sound switches
the device. Also VoodooHDA's DAC gain (`0x1E`, ≈ −43 dB) was far below Linux (`0x3B`).

Fix used here:

- VoodooHDA `NodesToPatch`: pin `0x17` → `0x411111f0` (not connected), so there is **one**
  speaker device. PCM mixer at 100.
- TAS2781DirectI2C mirrors DAC `0x02` (format `0x2`, stream/channel `0x6`, left/right amp gain
  `0xB`) onto DAC `0x03` every 500 ms while the speaker engine runs, and enables pin `0x17`
  (pin control `0x40`, connection 1, unmute, D0). Verbs go through the HDA immediate command
  interface (`ICOI 0x60`, `IRII 0x64`, `ICIS 0x68` in BAR0), alongside VoodooHDA.

## The driver (TAS2781DirectI2C 0.5.0)

- Matches the I²C0 PCI device, maps the DesignWare registers, uses polled transfers with the
  controller interrupts masked; the TX FIFO is filled with CPU interrupts disabled.
- 10 s after start: init. Then every 250 ms it watches the VoodooHDA engines (found by
  description: "Speaker" / "Head"): speaker engine running and headphones not → check amp health,
  re-init if needed, power up, mirror DAC 0x03. After 4 idle ticks → shutdown.
- Sleep: shutdown before sleep, re-init 3 s after wake.
- Commands (root) through `direct-control`: `status`, `reset`, `init`, `powerup`, `shutdown`,
  `atten=N`, `autooff=S`, `auto=0|1`, `dump=BB.PP`, `wr=CH.BB.PP.RR.VV`, `verb=HEX`,
  `codecdump`, `coefdump`.

## Building

The register tables are generated on the target machine because they contain its factory
calibration. See [drivers/TAS2781DirectI2C/README.md](../drivers/TAS2781DirectI2C/README.md).

## Open points

- Balance and system-sound behaviour after v0.5 not fully confirmed.
- Headphones: the driver keeps the amplifiers off while the headphone engine runs; jack detection
  is VoodooHDA's.
- Microphone not tested.
- VoodooI2C also attaches to the I²C0 controller. Concurrent access from VoodooI2C and
  TAS2781DirectI2C is not synchronised; it has caused no problems so far, but it is not proven safe.
