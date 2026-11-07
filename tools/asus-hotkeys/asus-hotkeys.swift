// ASUS-Hotkeys fuer den ROG Strix SCAR 18 G835LXG unter macOS.
// Liest die Fn-Tasten vom ASUS-Kanal der N-KEY-Tastatur (Report 0x5A):
//   Fn+M4 (0xAE)  Leistungsprofil Leise -> Leistung -> Turbo (ASUSG835WMI-Kext)
//   Fn+F2 (0xC5)  Tastaturlicht dunkler, Fn+F3 (0xC4) heller (Aura-Report, Stufe 0..3)
//   Fn+F7 (0x10)  Display dunkler, Fn+F8 (0x20) heller (Software-Abdunkelung per Farbkurve,
//                 weil der NVIDIA-Treiber fuer das interne Panel keine Helligkeit anbietet)
//   Fn+F4 (0xB3)  naechster Aura-Lichteffekt
// Tastenbelegung und Lichteffekte stehen in ~/Library/Application Support/ASUSHotkeys/config.json
// (wird bei jedem Tastendruck neu gelesen, Aenderungen brauchen keinen Neustart).
import AppKit
import CoreGraphics
import Foundation
import IOKit
import IOKit.hid

let defaults = UserDefaults(suiteName: "de.codex.asushotkeys")!
func log(_ s: String) { FileHandle.standardError.write("\(Date()) \(s)\n".data(using: .utf8)!) }

func notify(_ title: String, _ text: String) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
    p.arguments = ["-e", "display notification \"\(text)\" with title \"\(title)\""]
    try? p.run()
}

// MARK: Tastaturbeleuchtung (N-KEY, Feature-Report 0x5A)

var nkey: IOHIDDevice?
var kbdLevel = defaults.object(forKey: "kbd") as? Int ?? 2

func sendFeature(_ bytes: [UInt8]) -> IOReturn {
    guard let dev = nkey else { return kIOReturnNotOpen }
    var buf = bytes
    while buf.count < 64 { buf.append(0) }
    return IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, CFIndex(buf[0]), buf, buf.count)
}

func applyKeyboard() {
    let hello: [UInt8] = [0x5a] + Array("ASUS Tech.Inc.".utf8) + [0]
    let r1 = sendFeature(hello)
    let r2 = sendFeature([0x5a, 0xba, 0xc5, 0xc4, UInt8(kbdLevel)])
    log("Tastaturlicht Stufe \(kbdLevel) (\(String(format: "0x%x/0x%x", r1, r2)))")
}

func changeKeyboard(_ delta: Int) {
    kbdLevel = max(0, min(3, kbdLevel + delta))
    defaults.set(kbdLevel, forKey: "kbd")
    applyKeyboard()
}

// MARK: Einstellungsdatei

let configURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/ASUSHotkeys/config.json")
let defaultConfig = """
{
  "keys": { "ae": "profile", "c4": "kbd+", "c5": "kbd-", "20": "dim+", "10": "dim-", "b3": "aura" },
  "auraModes": [
    { "name": "Statisch weiss", "reports": ["5d b3 00 00 ff ff ff eb 00 00"] },
    { "name": "Atmen",          "reports": ["5d b3 00 01 ff ff ff eb 00 00 00 00 00"] },
    { "name": "Farbwechsel",    "reports": ["5d b3 00 02 ff 00 00 eb 00 00"] },
    { "name": "Regenbogen",     "reports": ["5d b3 00 03 ff 00 00 eb 00 00"] },
    { "name": "Statisch rot",   "reports": ["5d b3 00 00 ff 00 00 eb 00 00"] }
  ],
  "auraApply": ["5d b5", "5d b4"]
}
"""

struct AuraMode: Codable { var name: String; var reports: [String] }
struct Config: Codable { var keys: [String: String]; var auraModes: [AuraMode]; var auraApply: [String] }

func loadConfig() -> Config {
    if !FileManager.default.fileExists(atPath: configURL.path) {
        try? defaultConfig.data(using: .utf8)!.write(to: configURL)
    }
    if let data = try? Data(contentsOf: configURL), let c = try? JSONDecoder().decode(Config.self, from: data) {
        return c
    }
    log("config.json fehlerhaft, verwende Standard")
    return try! JSONDecoder().decode(Config.self, from: defaultConfig.data(using: .utf8)!)
}

func hexBytes(_ s: String) -> [UInt8] { s.split(separator: " ").compactMap { UInt8($0, radix: 16) } }

// MARK: Aura-Lichteffekte

var auraIndex = defaults.object(forKey: "aura") as? Int ?? -1   // -1: nicht gesetzt, Tastatur behaelt ihren Effekt

func applyAura(_ config: Config) {
    guard auraIndex >= 0, !config.auraModes.isEmpty else { return }
    let mode = config.auraModes[auraIndex % config.auraModes.count]
    var codes: [String] = []
    for r in mode.reports + config.auraApply { codes.append(String(format: "0x%x", sendFeature(hexBytes(r)))) }
    _ = sendFeature([0x5a, 0xba, 0xc5, 0xc4, UInt8(kbdLevel)])
    log("Aura \(mode.name): \(codes.joined(separator: " "))")
}

func nextAura() {
    let config = loadConfig()
    guard !config.auraModes.isEmpty else { return }
    auraIndex = (auraIndex + 1) % config.auraModes.count
    defaults.set(auraIndex, forKey: "aura")
    applyAura(config)
    notify("Tastaturbeleuchtung", config.auraModes[auraIndex].name)
}

// MARK: Leistungsprofil (ASUSG835WMI-Kext)

let profiles = ["quiet", "balanced", "performance"]
let profileNames = ["quiet": "Leise", "balanced": "Leistung", "performance": "Turbo"]

func wmiService() -> io_service_t {
    IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("ASUSG835WMI"))
}

func currentProfile() -> String? {
    let s = wmiService()
    guard s != 0 else { return nil }
    defer { IOObjectRelease(s) }
    _ = IORegistryEntrySetCFProperties(s, ["ASUSCommand": "profile?"] as CFDictionary)
    return IORegistryEntryCreateCFProperty(s, "Profile" as CFString, kCFAllocatorDefault, 0)?
        .takeRetainedValue() as? String
}

func cycleProfile() {
    let s = wmiService()
    guard s != 0 else { notify("Leistungsprofil", "ASUS-WMI-Treiber nicht geladen"); return }
    defer { IOObjectRelease(s) }
    let cur = currentProfile() ?? "balanced"
    let next = profiles[((profiles.firstIndex(of: cur) ?? 1) + 1) % profiles.count]
    let r = IORegistryEntrySetCFProperties(s, ["ASUSCommand": "profile=\(next)"] as CFDictionary)
    let now = currentProfile() ?? "?"
    log("Profil \(cur) -> \(next): \(String(format: "0x%x", r)), gelesen \(now)")
    notify("Leistungsprofil", r == KERN_SUCCESS ? (profileNames[now] ?? now) : "Umschalten fehlgeschlagen")
}

// MARK: Displayhelligkeit (Software-Abdunkelung)

var dimLevel = defaults.object(forKey: "dim") as? Int ?? 10   // 0..10, 10 = volle Helligkeit

func applyDim() {
    let factor = Float(0.25 + 0.075 * Double(dimLevel))       // 25 % .. 100 %
    var count: UInt32 = 0
    CGGetOnlineDisplayList(0, nil, &count)
    var ids = [CGDirectDisplayID](repeating: 0, count: Int(count))
    CGGetOnlineDisplayList(count, &ids, &count)
    for d in ids where CGDisplayIsBuiltin(d) != 0 {
        if dimLevel >= 10 {
            CGDisplayRestoreColorSyncSettings()
        } else {
            CGSetDisplayTransferByFormula(d, 0, factor, 1, 0, factor, 1, 0, factor, 1)
        }
    }
}

func changeDim(_ delta: Int) {
    dimLevel = max(0, min(10, dimLevel + delta))
    defaults.set(dimLevel, forKey: "dim")
    applyDim()
    log("Display-Stufe \(dimLevel)")
}

// MARK: HID-Eingabe

var lastCode: UInt8 = 0
let reportBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: 128)

func handle(_ code: UInt8) {
    let action = loadConfig().keys[String(format: "%02x", code)]
    switch action {
    case "profile": cycleProfile()
    case "kbd+": changeKeyboard(+1)
    case "kbd-": changeKeyboard(-1)
    case "dim+": changeDim(+1)
    case "dim-": changeDim(-1)
    case "aura": nextAura()
    default: log(String(format: "Taste 0x%02x ohne Belegung", code))
    }
}

let manager = IOHIDManagerCreate(kCFAllocatorDefault, 0)
IOHIDManagerSetDeviceMatching(manager, [kIOHIDVendorIDKey: 0x0b05, kIOHIDProductIDKey: 0x19b6] as CFDictionary)
IOHIDManagerRegisterDeviceMatchingCallback(manager, { _, _, _, device in
    nkey = device
    IOHIDDeviceRegisterInputReportCallback(device, reportBuffer, 128, { _, _, _, _, id, report, len in
        guard id == 0x5a, len >= 2 else { return }
        let code = report[1]
        // Nur beim Druecken reagieren; Wiederholungen beim Halten (gleicher Code) ignorieren.
        if code != 0 && code != lastCode { handle(code) }
        lastCode = code
    }, nil)
    log("N-KEY verbunden")
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) { applyKeyboard(); applyAura(loadConfig()) }
}, nil)
IOHIDManagerRegisterDeviceRemovalCallback(manager, { _, _, _, _ in nkey = nil }, nil)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
let openResult = IOHIDManagerOpen(manager, 0)
log(String(format: "IOHIDManagerOpen 0x%x", openResult))
if openResult != kIOReturnSuccess {
    notify("ASUS-Hotkeys", "Bitte unter Datenschutz & Sicherheit > Eingabeueberwachung erlauben")
}

// Nach dem Aufwachen und bei Display-Aenderungen Zustand wiederherstellen.
NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { applyKeyboard(); applyAura(loadConfig()); applyDim() }
}
CGDisplayRegisterReconfigurationCallback({ _, flags, _ in
    if !flags.contains(.beginConfigurationFlag) { DispatchQueue.main.async { applyDim() } }
}, nil)
// Manche Systemdienste setzen die Farbkurve zurueck; regelmaessig nachziehen.
Timer.scheduledTimer(withTimeInterval: 15, repeats: true) { _ in if dimLevel < 10 { applyDim() } }
applyDim()
log("ASUS-Hotkeys gestartet, Profil \(currentProfile() ?? "unbekannt")")
CFRunLoopRun()
