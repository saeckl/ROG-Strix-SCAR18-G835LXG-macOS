// ASUS-Hotkeys fuer den ROG Strix SCAR 18 G835LXG unter macOS (Version 3).
// Liest die Fn-Tasten vom ASUS-Kanal der N-KEY-Tastatur (Report 0x5A):
//   Fn+M4 (0xAE)  Leistungsprofil Leise -> Leistung -> Turbo (ASUSG835WMI-Kext)
//   Fn+F2 (0xC5)  Tastaturlicht dunkler, Fn+F3 (0xC4) heller (Stufe 0..3)
//   Fn+F7 (0x10)  Display dunkler, Fn+F8 (0x20) heller (echte Hintergrundbeleuchtung, sonst Farbkurve)
//                 (Hintergrundbeleuchtung braucht NullMoth >= 2.8 mit boot-arg nvrmacpibl=1)
//   Fn+F4 (0xB3)  naechster Aura-Effekt
// Beleuchtung (Farbe, Effekt, Zonen) steht in lighting.json und wird von der App "ROG Licht"
// geaendert; die App schickt dann die Mitteilung de.codex.asushotkeys.lighting.
// Tastenbelegung: ~/Library/Application Support/ASUSHotkeys/config.json.
// Bauen: swiftc -O asus-hotkeys.swift Lighting.swift -o asus-hotkeys
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

// MARK: N-KEY Feature-Reports

var nkey: IOHIDDevice?

func sendFeature(_ bytes: [UInt8]) -> IOReturn {
    guard let dev = nkey else { return kIOReturnNotOpen }
    var buf = bytes
    while buf.count < 64 { buf.append(0) }
    return IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, CFIndex(buf[0]), buf, buf.count)
}

// MARK: Beleuchtung

var lighting: LightingState = {
    if let s = loadLighting() { return s }
    var s = LightingState()                     // Erststart: bisherige Helligkeit uebernehmen
    s.brightness = defaults.object(forKey: "kbd") as? Int ?? 3
    saveLighting(s)
    return s
}()

func applyBrightness() {
    let r = sendFeature([0x5a, 0xba, 0xc5, 0xc4, UInt8(max(0, min(3, lighting.brightness)))])
    log("Tastaturlicht Stufe \(lighting.brightness) (\(String(format: "0x%x", r)))")
}

func applyLighting() {
    let hello: [UInt8] = [0x5a] + Array("ASUS Tech.Inc.".utf8) + [0]
    var codes: [String] = []
    for r in [hello, lighting.powerReport(), lighting.effectReport(), [0x5d, 0xb5], [0x5d, 0xb4]] {
        codes.append(String(format: "0x%x", sendFeature(r)))
    }
    applyBrightness()
    log("Beleuchtung \(lighting.modeName) \(lighting.color) Zonen K\(lighting.keyboard.awake ? 1 : 0) " +
        "L\(lighting.lightbar.awake ? 1 : 0) O\(lighting.logo.awake ? 1 : 0): \(codes.joined(separator: " "))")
}

func changed() {
    saveLighting(lighting)
    DistributedNotificationCenter.default().postNotificationName(lightingChangedNotification, object: nil,
                                                                 userInfo: nil, deliverImmediately: true)
}

func changeKeyboard(_ delta: Int) {
    lighting.brightness = max(0, min(3, lighting.brightness + delta))
    applyBrightness()
    changed()
}

func nextAura() {
    let ids = auraModes.map(\.id)
    let i = ids.firstIndex(of: lighting.mode) ?? -1
    lighting.mode = ids[(i + 1) % ids.count]
    applyLighting()
    changed()
    notify("Tastaturbeleuchtung", lighting.modeName)
}

// MARK: Tastenbelegung

let configURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/ASUSHotkeys/config.json")
let defaultKeys = ["ae": "profile", "c4": "kbd+", "c5": "kbd-", "20": "dim+", "10": "dim-", "b3": "aura"]

func loadKeys() -> [String: String] {
    if !FileManager.default.fileExists(atPath: configURL.path),
       let data = try? JSONSerialization.data(withJSONObject: ["keys": defaultKeys], options: .prettyPrinted) {
        try? data.write(to: configURL)
    }
    if let data = try? Data(contentsOf: configURL),
       let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
       let keys = obj["keys"] as? [String: String] {
        return keys
    }
    log("config.json fehlerhaft, verwende Standard")
    return defaultKeys
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
    changed()
}

// MARK: Displayhelligkeit
// Mit NullMoth 2.8 und boot-arg nvrmacpibl=1 hat das Panel eine echte Hintergrundbeleuchtung
// (AppleBacklightDisplay). Dann regeln Fn+F7/F8 sie ueber DisplayServices wie der Systemregler
// (16 Stufen). Ohne Hintergrundbeleuchtung bleibt die Software-Abdunkelung per Farbkurve.

typealias DSCanFn = @convention(c) (UInt32) -> Bool
typealias DSGetFn = @convention(c) (UInt32, UnsafeMutablePointer<Float>) -> Int32
typealias DSSetFn = @convention(c) (UInt32, Float) -> Int32
let ds = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_NOW)
func dsSym<T>(_ n: String, _ t: T.Type) -> T? { ds.flatMap { dlsym($0, n) }.map { unsafeBitCast($0, to: t) } }
let dsCan = dsSym("DisplayServicesCanChangeBrightness", DSCanFn.self)
let dsGet = dsSym("DisplayServicesGetBrightness", DSGetFn.self)
let dsSet = dsSym("DisplayServicesSetBrightness", DSSetFn.self)

func builtinDisplay() -> CGDirectDisplayID? {
    var count: UInt32 = 0
    CGGetOnlineDisplayList(0, nil, &count)
    var ids = [CGDirectDisplayID](repeating: 0, count: Int(count))
    CGGetOnlineDisplayList(count, &ids, &count)
    return ids.first { CGDisplayIsBuiltin($0) != 0 }
}

func hasBacklight() -> Bool {
    guard let d = builtinDisplay(), let can = dsCan, dsGet != nil, dsSet != nil else { return false }
    return can(d)
}

func changeBacklight(_ delta: Int) -> Bool {
    guard hasBacklight(), let d = builtinDisplay(), let get = dsGet, let set = dsSet else { return false }
    var cur: Float = 1
    _ = get(d, &cur)
    let step = (cur * 16).rounded() + Float(delta)
    let value = max(0, min(16, step)) / 16
    let r = set(d, value)
    log("Hintergrundbeleuchtung \(Int(value * 100)) % (\(r))")
    return true
}

var dimLevel = defaults.object(forKey: "dim") as? Int ?? 10   // 0..10, 10 = volle Helligkeit

func applyDim() {
    if hasBacklight() {                      // echte Hintergrundbeleuchtung: keine Farbkurve
        if dimLevel < 10 { dimLevel = 10; defaults.set(10, forKey: "dim") }
        CGDisplayRestoreColorSyncSettings()
        return
    }
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
    if changeBacklight(delta) { return }
    dimLevel = max(0, min(10, dimLevel + delta))
    defaults.set(dimLevel, forKey: "dim")
    applyDim()
    log("Display-Stufe \(dimLevel)")
}

// MARK: HID-Eingabe

var lastCode: UInt8 = 0
let reportBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: 128)

func handle(_ code: UInt8) {
    switch loadKeys()[String(format: "%02x", code)] {
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
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) { applyLighting() }
}, nil)
IOHIDManagerRegisterDeviceRemovalCallback(manager, { _, _, _, _ in nkey = nil }, nil)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
let openResult = IOHIDManagerOpen(manager, 0)
log(String(format: "IOHIDManagerOpen 0x%x", openResult))
if openResult != kIOReturnSuccess {
    notify("ASUS-Hotkeys", "Bitte unter Datenschutz & Sicherheit > Eingabeueberwachung erlauben")
}

// Die App "ROG Licht" hat lighting.json geaendert.
DistributedNotificationCenter.default().addObserver(forName: lightingNotification, object: nil, queue: .main) { _ in
    if let s = loadLighting() { lighting = s; applyLighting() }
}

// Nach dem Aufwachen und bei Display-Aenderungen Zustand wiederherstellen.
NSWorkspace.shared.notificationCenter.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { _ in
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { applyLighting(); applyDim() }
}
CGDisplayRegisterReconfigurationCallback({ _, flags, _ in
    if !flags.contains(.beginConfigurationFlag) { DispatchQueue.main.async { applyDim() } }
}, nil)
// Manche Systemdienste setzen die Farbkurve zurueck; regelmaessig nachziehen.
Timer.scheduledTimer(withTimeInterval: 15, repeats: true) { _ in if dimLevel < 10 { applyDim() } }
applyDim()
log("ASUS-Hotkeys 3 gestartet, Profil \(currentProfile() ?? "unbekannt")")
CFRunLoopRun()
