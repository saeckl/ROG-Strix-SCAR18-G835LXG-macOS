// Legt ein Multi-Output-Gerät aus beiden VoodooHDA-Lautsprecherausgängen (Pin 0x14 und 0x17)
// an und setzt es als Standardausgang. Aufruf: swift speaker-multiout.swift [create|remove|list]
import CoreAudio
import Foundation

func prop<T>(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector, _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal, _ value: inout T) -> OSStatus {
    var addr = AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    var size = UInt32(MemoryLayout<T>.size)
    return AudioObjectGetPropertyData(id, &addr, 0, nil, &size, &value)
}
func string(_ id: AudioObjectID, _ sel: AudioObjectPropertySelector) -> String {
    var s: CFString = "" as CFString
    _ = prop(id, sel, kAudioObjectPropertyScopeGlobal, &s)
    return s as String
}
func devices() -> [AudioObjectID] {
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    var size: UInt32 = 0
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size)
    var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids)
    return ids
}
let aggUID = "org.codex.tas2781.speakers"
let mode = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "create"
for d in devices() { print(d, string(d, kAudioObjectPropertyName), "|", string(d, kAudioDevicePropertyDeviceUID)) }
if mode == "list" { exit(0) }
if mode.hasPrefix("default=") {
    let uid = String(mode.dropFirst(8))
    guard var dev = devices().first(where: { string($0, kAudioDevicePropertyDeviceUID) == uid }) else { print("nicht gefunden", uid); exit(1) }
    var a = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
    print("Standardausgang", uid, AudioObjectSetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, UInt32(MemoryLayout<AudioObjectID>.size), &dev))
    exit(0)
}
for d in devices() where string(d, kAudioDevicePropertyDeviceUID) == aggUID {
    print("entferne altes Aggregat", d, AudioHardwareDestroyAggregateDevice(d))
}
if mode == "remove" { exit(0) }
let speakers = devices().filter { string($0, kAudioObjectPropertyName).contains("Speaker (Analog)") }
                        .map { string($0, kAudioDevicePropertyDeviceUID) }.sorted()
guard speakers.count == 2 else { print("erwartet 2 Lautsprecherausgaenge, gefunden:", speakers); exit(1) }
let desc: [String: Any] = [
    kAudioAggregateDeviceNameKey: "TAS2781 Lautsprecher",
    kAudioAggregateDeviceUIDKey: aggUID,
    kAudioAggregateDeviceIsStackedKey: 1,
    kAudioAggregateDeviceMainSubDeviceKey: speakers[0],
    kAudioAggregateDeviceSubDeviceListKey: speakers.map { [kAudioSubDeviceUIDKey: $0] },
]
var agg: AudioObjectID = 0
let st = AudioHardwareCreateAggregateDevice(desc as CFDictionary, &agg)
print("Aggregat", agg, "status", st, "aus", speakers)
guard st == 0 else { exit(1) }
Thread.sleep(forTimeInterval: 1)
var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
print("Standardausgang gesetzt:", AudioObjectSetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, UInt32(MemoryLayout<AudioObjectID>.size), &agg))
