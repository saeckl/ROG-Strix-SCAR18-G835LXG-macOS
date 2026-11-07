# Diagnostics

| File | Purpose | Build |
| --- | --- | --- |
| `hidlog.swift` | print N-KEY (`0b05:19b6`) input reports, to find key codes | `swiftc -O hidlog.swift -o hidlog` (needs Input Monitoring) |
| `aura.swift` | send N-KEY feature reports, e.g. `./aura 5d b3 00 03 ff 00 00 eb` | `swiftc -O aura.swift -o aura` |
| `speaker-multiout.swift` | list audio devices, create/remove a multi-output device, set default | `swiftc -O speaker-multiout.swift -o speaker-multiout` |
| `get-acpi.c` | dump ACPI tables from the IORegistry | `clang -O2 get-acpi.c -framework IOKit -framework CoreFoundation -o get-acpi` |
