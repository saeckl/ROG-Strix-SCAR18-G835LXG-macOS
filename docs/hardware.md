# Hardware: ASUS ROG Strix SCAR 18 G835LXG

From Linux (Ubuntu, kernel 7.0) and macOS on BIOS `G835LXG.306`.

| Function | Device |
| --- | --- |
| CPU | Intel Core Ultra 9 290HX Plus (Arrow Lake-HX), 24 threads; 2 × 32 GB DDR5-6400 |
| Graphics | Intel Arrow Lake-S iGPU `8086:7d67`; NVIDIA RTX 5090 Laptop `10de:2c58` (GB203), HDMI/DP audio `10de:22e9` |
| Audio | Intel HDA `8086:7f50`, Realtek ALC285 (subsystem `0x10433f10`), 2× TI TAS2781 on I²C0 `0x38`/`0x39` |
| I²C | DesignWare LPSS `8086:7f4c` (I²C0 at `\_SB.PC02.I2C0`, amplifiers) and siblings |
| Storage | Samsung MZVLC4T0HBL1 4 TB (`144d:a810`), Samsung 970 PRO 512 GB (`144d:a808`) |
| Wi-Fi / BT | Intel BE200 Wi-Fi 7 `8086:272b`, Intel Bluetooth `8087:0036` |
| Ethernet | Realtek RTL8125 2.5 GbE `10ec:8125` |
| USB / TB | Intel PCH xHCI `8086:7f6e`, TB4 `8086:7ec0`, JHL9580 TB5 `8086:5780-5782` |
| Input | ASUS N-KEY `0b05:19b6` (keyboard + Fn keys + Aura), ITE 8295 `0b05:193b`, touchpad `ASUF1209` (`2808:0219`, I²C HID) |
| Camera | Quanta ASUS FHD webcam `0408:30ed` |
| ASUS-WMI | `\_SB.ATKD`, GUID `97845ED0-4E6D-11DE-8A39-0800200C9A66`, event GUID `0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C` |

Linux driver references: [tas2781 HDA](https://github.com/torvalds/linux/tree/master/sound/hda/codecs/side-codecs),
[asus-wmi.h](https://github.com/torvalds/linux/blob/master/include/linux/platform_data/x86/asus-wmi.h),
[hid-asus.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-asus.c),
[asusctl](https://gitlab.com/asus-linux/asusctl).
