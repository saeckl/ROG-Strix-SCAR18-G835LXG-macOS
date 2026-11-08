#!/bin/bash
# Builds ASUSG835WMI.kext and the wmictl test tool. Needs the Xcode Command Line Tools.
set -euo pipefail
cd "$(dirname "$0")"
VERSION=0.1.4
mkdir -p build
sdk=$(xcrun --sdk macosx --show-sdk-path)
res=$(clang -print-resource-dir)
flags=(-arch x86_64 -mmacosx-version-min=26.0 -mkernel -nostdinc -isystem "$res/include"
       -I "$sdk/System/Library/Frameworks/Kernel.framework/Headers" -DKERNEL -DKERNEL_PRIVATE -DAPPLE -DNeXT)
clang++ "${flags[@]}" -fapple-kext -fno-rtti -fno-exceptions -std=c++17 -O2 -c ASUSG835WMI.cpp -o build/wmi.o
clang "${flags[@]}" -DKEXT_ID=org.codex.asusg835wmi "-DKEXT_VERSION=\"$VERSION\"" \
    -c ../../common/kmod_info.c -o build/kmod.o
kext=build/ASUSG835WMI.kext
rm -rf "$kext"; mkdir -p "$kext/Contents/MacOS"
cp Info.plist "$kext/Contents/Info.plist"
clang++ -arch x86_64 -mmacosx-version-min=26.0 -nostdlib -Xlinker -kext -Xlinker -export_dynamic \
    build/wmi.o build/kmod.o -L"$sdk/usr/lib" -lkmodc++ -lkmod -o "$kext/Contents/MacOS/ASUSG835WMI"
plutil -lint "$kext/Contents/Info.plist"
clang -O2 -o build/wmictl wmictl.c -framework IOKit -framework CoreFoundation
echo "Built $kext and build/wmictl"
