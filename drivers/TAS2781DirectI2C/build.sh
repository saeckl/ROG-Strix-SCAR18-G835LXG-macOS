#!/bin/bash
# Builds TAS2781DirectI2C.kext. Needs the Xcode Command Line Tools, python3, the two TI
# firmware files in ./firmware (see firmware/README.md) and must run on the target laptop:
# the factory calibration is read from this machine's UEFI variable CALI_DATA.
set -euo pipefail
cd "$(dirname "$0")"
VERSION=0.5.2
mkdir -p build
python3 generate_test_tables.py          # -> TAS2781TestTables.hpp (contains YOUR calibration)
sdk=$(xcrun --sdk macosx --show-sdk-path)
res=$(clang -print-resource-dir)
flags=(-arch x86_64 -mmacosx-version-min=26.0 -mkernel -nostdinc -isystem "$res/include"
       -I "$sdk/System/Library/Frameworks/Kernel.framework/Headers" -DKERNEL -DKERNEL_PRIVATE -DAPPLE -DNeXT)
clang++ "${flags[@]}" -fapple-kext -fno-rtti -fno-exceptions -std=c++17 -O2 \
    -c TAS2781DirectI2C.cpp -o build/driver.o
clang "${flags[@]}" -DKEXT_ID=org.codex.tas2781directi2c "-DKEXT_VERSION=\"$VERSION\"" \
    -c ../../common/kmod_info.c -o build/kmod.o
kext=build/TAS2781DirectI2C.kext
rm -rf "$kext"; mkdir -p "$kext/Contents/MacOS"
cp Info.plist "$kext/Contents/Info.plist"
clang++ -arch x86_64 -mmacosx-version-min=26.0 -nostdlib -Xlinker -kext -Xlinker -export_dynamic \
    build/driver.o build/kmod.o -L"$sdk/usr/lib" -lkmodc++ -lkmod -o "$kext/Contents/MacOS/TAS2781DirectI2C"
plutil -lint "$kext/Contents/Info.plist"
clang -O2 -o build/direct-control direct-control.c -framework IOKit -framework CoreFoundation
echo "Built $kext and build/direct-control"
