#!/bin/bash
# Builds NVPCFBridge.kext (Lilu plugin) with the Command Line Tools. SDKs in ./sdk (Lilu 1.7.2 DEBUG, MacKernelSDK).
set -euo pipefail
cd "$(dirname "$0")"
L=sdk/lilu/Lilu.kext/Contents/Resources; K=sdk/MacKernelSDK
mkdir -p build
CF=(-target x86_64-apple-macos10.15 -mkernel -nostdinc -fno-builtin -fno-common -fno-stack-protector -O2 -Wno-deprecated-declarations
    -I"$K/Headers" -I"$L" -isystem "$(clang -print-resource-dir)/include" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
    -DPRODUCT_NAME=NVPCFBridge -DMODULE_VERSION=1.0.0 -Das_saeckl_NVPCFBridge)
clang++ "${CF[@]}" -fapple-kext -std=c++17 -nostdinc++ -fno-rtti -fno-exceptions -fno-use-cxa-atexit -c NVPCFBridge.cpp -o build/bridge.o
clang++ "${CF[@]}" -fapple-kext -std=c++17 -nostdinc++ -fno-rtti -fno-exceptions -fno-use-cxa-atexit -c "$L/Library/plugin_start.cpp" -o build/plugin_start.o
clang "${CF[@]}" -std=gnu11 -c kmod_info.c -o build/kmod_info.o
K2=build/NVPCFBridge.kext; rm -rf "$K2"; mkdir -p "$K2/Contents/MacOS"; cp Info.plist "$K2/Contents/Info.plist"
clang++ -target x86_64-apple-macos10.15 -nostdlib -Xlinker -kext -Xlinker -export_dynamic build/bridge.o build/plugin_start.o build/kmod_info.o \
    "$K/Library/x86_64/libkmod.a" -o "$K2/Contents/MacOS/NVPCFBridge"
plutil -lint "$K2/Contents/Info.plist"
nm -u "$K2/Contents/MacOS/NVPCFBridge" | head -40
