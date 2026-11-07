// Shared lighting model for the ASUS hotkey agent and the "ROG Licht" menu bar app.
// State lives in ~/Library/Application Support/ASUSHotkeys/lighting.json.
// Protocol (N-KEY 0b05:19b6, feature reports), from asusctl rog-aura:
//   effect  5d b3 <zone 0> <mode> R G B <speed> <dir> 00 R2 G2 B2 00 00 00 00
//   set     5d b5, apply 5d b4
//   power   5d bd 01 b0 b1 b2 b3  (keyboard/logo/lightbar x boot/awake/sleep/shutdown)
//   bright  5a ba c5 c4 <0..3>
import Foundation

struct ZonePower: Codable, Equatable {
    var awake = true, boot = true, sleep = false, shutdown = false
}

struct LightingState: Codable, Equatable {
    var mode = 0                    // AuraModeNum
    var color: [Int] = [255, 255, 255]
    var color2: [Int] = [0, 0, 0]
    var speed = 1                   // 0 slow, 1 medium, 2 fast
    var direction = 0               // 0 right, 1 left, 2 up, 3 down
    var brightness = 3              // 0..3
    var keyboard = ZonePower(shutdown: true)   // like Armoury Crate: boot/shutdown on, sleep off
    var lightbar = ZonePower(awake: false, boot: false)
    var logo = ZonePower(awake: false, boot: false)
}

struct AuraMode { let id: Int; let name: String; let colors: Int; let direction: Bool }

let auraModes: [AuraMode] = [
    AuraMode(id: 0, name: "Statisch", colors: 1, direction: false),
    AuraMode(id: 1, name: "Atmen", colors: 2, direction: false),
    AuraMode(id: 2, name: "Farbwechsel", colors: 0, direction: false),
    AuraMode(id: 3, name: "Regenbogenwelle", colors: 0, direction: true),
    AuraMode(id: 4, name: "Sterne", colors: 2, direction: false),
    AuraMode(id: 5, name: "Regen", colors: 0, direction: false),
    AuraMode(id: 6, name: "Hervorheben", colors: 1, direction: false),
    AuraMode(id: 7, name: "Laser", colors: 1, direction: false),
    AuraMode(id: 8, name: "Kräuselung", colors: 1, direction: false),
    AuraMode(id: 10, name: "Pulsieren", colors: 1, direction: false),
    AuraMode(id: 11, name: "Komet", colors: 1, direction: false),
    AuraMode(id: 12, name: "Flash", colors: 1, direction: false),
]

let lightingNotification = Notification.Name("de.codex.asushotkeys.lighting")       // app -> agent: apply
let lightingChangedNotification = Notification.Name("de.codex.asushotkeys.changed")  // agent -> app: reload

let lightingURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Library/Application Support/ASUSHotkeys/lighting.json")

func loadLighting() -> LightingState? {
    guard let data = try? Data(contentsOf: lightingURL) else { return nil }
    return try? JSONDecoder().decode(LightingState.self, from: data)
}

func saveLighting(_ s: LightingState) {
    let enc = JSONEncoder()
    enc.outputFormatting = [.prettyPrinted, .sortedKeys]
    if let data = try? enc.encode(s) {
        try? FileManager.default.createDirectory(at: lightingURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? data.write(to: lightingURL, options: .atomic)
    }
}

extension LightingState {
    func powerReport() -> [UInt8] {
        func bit(_ on: Bool, _ n: Int) -> UInt32 { on ? 1 << UInt32(n) : 0 }
        var a: UInt32 = 0
        a |= bit(keyboard.boot, 1) | bit(keyboard.awake, 3) | bit(keyboard.sleep, 5) | bit(keyboard.shutdown, 7)
        a |= bit(logo.boot, 0) | bit(logo.awake, 2) | bit(logo.sleep, 4) | bit(logo.shutdown, 6)
        a |= bit(lightbar.boot, 9) | bit(lightbar.awake, 10) | bit(lightbar.sleep, 11) | bit(lightbar.shutdown, 12)
        return [0x5d, 0xbd, 0x01, UInt8(a & 0xff), UInt8((a >> 8) & 0xff), UInt8((a >> 16) & 0xff), UInt8(a >> 24)]
    }

    func effectReport() -> [UInt8] {
        func c(_ v: [Int], _ i: Int) -> UInt8 { UInt8(max(0, min(255, i < v.count ? v[i] : 0))) }
        let speeds: [UInt8] = [0xe1, 0xeb, 0xf5]
        return [0x5d, 0xb3, 0x00, UInt8(mode), c(color, 0), c(color, 1), c(color, 2),
                speeds[max(0, min(2, speed))], UInt8(max(0, min(3, direction))), 0x00,
                c(color2, 0), c(color2, 1), c(color2, 2), 0, 0, 0, 0]
    }

    var modeName: String { auraModes.first { $0.id == mode }?.name ?? "Modus \(mode)" }
}
