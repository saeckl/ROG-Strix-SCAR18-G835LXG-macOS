// ROG Licht: Menueleisten-App fuer Tastaturbeleuchtung, Lichtzonen und Leistungsprofil
// des ASUS ROG Strix SCAR 18 G835LXG. Schreibt lighting.json und bittet das Hintergrundprogramm
// asus-hotkeys per Mitteilung, die Einstellung an die Tastatur zu senden (das Programm hat die
// Eingabeueberwachung; die App braucht keine Freigabe).
// Bauen: swiftc -O -parse-as-library ROGLicht.swift Lighting.swift -o ROGLicht
import AppKit
import IOKit
import SwiftUI

final class Model: ObservableObject {
    @Published var s: LightingState { didSet { if s != oldValue && !loading { scheduleApply() } } }
    @Published var profile = "balanced"
    @Published var agentRunning = true
    private var loading = false
    private var pending: DispatchWorkItem?

    init() {
        s = loadLighting() ?? LightingState()
        profile = Model.readProfile() ?? "balanced"
        DistributedNotificationCenter.default().addObserver(forName: lightingChangedNotification, object: nil,
                                                            queue: .main) { [weak self] _ in self?.reload() }
    }

    func reload() {
        loading = true
        if let n = loadLighting(), n != s { s = n }
        loading = false
        profile = Model.readProfile() ?? profile
        agentRunning = agentAlive()
    }

    private func agentAlive() -> Bool {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
        p.arguments = ["-x", "asus-hotkeys"]
        try? p.run(); p.waitUntilExit()
        return p.terminationStatus == 0
    }

    private func scheduleApply() {
        pending?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            saveLighting(self.s)
            DistributedNotificationCenter.default().postNotificationName(lightingNotification, object: nil,
                                                                         userInfo: nil, deliverImmediately: true)
        }
        pending = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: work)
    }

    static func readProfile() -> String? {
        let svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("ASUSG835WMI"))
        guard svc != 0 else { return nil }
        defer { IOObjectRelease(svc) }
        _ = IORegistryEntrySetCFProperties(svc, ["ASUSCommand": "profile?"] as CFDictionary)
        return IORegistryEntryCreateCFProperty(svc, "Profile" as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue() as? String
    }

    func setProfile(_ p: String) {
        let svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("ASUSG835WMI"))
        guard svc != 0 else { return }
        defer { IOObjectRelease(svc) }
        _ = IORegistryEntrySetCFProperties(svc, ["ASUSCommand": "profile=\(p)"] as CFDictionary)
        profile = Model.readProfile() ?? p
    }

    var mode: AuraMode { auraModes.first { $0.id == s.mode } ?? auraModes[0] }
}

func color(_ v: [Int]) -> Color {
    Color(red: Double(v[0]) / 255, green: Double(v[1]) / 255, blue: Double(v[2]) / 255)
}

func rgb(_ c: Color) -> [Int] {
    let n = NSColor(c).usingColorSpace(.sRGB) ?? .white
    return [n.redComponent, n.greenComponent, n.blueComponent].map { Int(($0 * 255).rounded()) }
}

let presets: [[Int]] = [[255, 0, 0], [255, 96, 0], [255, 255, 0], [0, 255, 0],
                        [0, 255, 255], [0, 0, 255], [255, 0, 255], [255, 255, 255]]

struct Swatches: View {
    @Binding var value: [Int]
    var body: some View {
        HStack(spacing: 6) {
            ForEach(presets, id: \.self) { p in
                Button { value = p } label: {
                    RoundedRectangle(cornerRadius: 4).fill(color(p)).frame(width: 22, height: 22)
                        .overlay(RoundedRectangle(cornerRadius: 4)
                            .stroke(p == value ? Color.primary : Color.secondary.opacity(0.4), lineWidth: p == value ? 2 : 1))
                }.buttonStyle(.plain)
            }
            ColorPicker("", selection: Binding(get: { color(value) }, set: { value = rgb($0) }),
                        supportsOpacity: false).labelsHidden()
        }
    }
}

struct ZoneRow: View {
    let name: String
    @Binding var zone: ZonePower
    var expanded: Bool
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Toggle(name, isOn: $zone.awake)
            if expanded {
                HStack(spacing: 12) {
                    Toggle("Start", isOn: $zone.boot)
                    Toggle("Ruhezustand", isOn: $zone.sleep)
                    Toggle("Herunterfahren", isOn: $zone.shutdown)
                }.font(.caption).padding(.leading, 18)
            }
        }
    }
}

struct Panel: View {
    @ObservedObject var m: Model
    @State private var more = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("ROG Licht").font(.headline)
                Spacer()
                if !m.agentRunning {
                    Text("Hintergrundprogramm läuft nicht").font(.caption).foregroundStyle(.red)
                }
            }

            Picker("Leistung", selection: Binding(get: { m.profile }, set: { m.setProfile($0) })) {
                Text("Leise").tag("quiet"); Text("Leistung").tag("balanced"); Text("Turbo").tag("performance")
            }.pickerStyle(.segmented)

            Divider()

            Picker("Effekt", selection: $m.s.mode) {
                ForEach(auraModes, id: \.id) { Text($0.name).tag($0.id) }
            }

            if m.mode.colors >= 1 {
                Text(m.mode.colors == 2 ? "Farbe 1" : "Farbe").font(.caption).foregroundStyle(.secondary)
                Swatches(value: $m.s.color)
            }
            if m.mode.colors == 2 {
                Text("Farbe 2").font(.caption).foregroundStyle(.secondary)
                Swatches(value: $m.s.color2)
            }
            if m.s.mode != 0 {
                Picker("Tempo", selection: $m.s.speed) {
                    Text("Langsam").tag(0); Text("Mittel").tag(1); Text("Schnell").tag(2)
                }.pickerStyle(.segmented)
            }
            if m.mode.direction {
                Picker("Richtung", selection: $m.s.direction) {
                    Text("→").tag(0); Text("←").tag(1); Text("↑").tag(2); Text("↓").tag(3)
                }.pickerStyle(.segmented)
            }
            Picker("Helligkeit", selection: $m.s.brightness) {
                Text("Aus").tag(0); Text("1").tag(1); Text("2").tag(2); Text("3").tag(3)
            }.pickerStyle(.segmented)

            Divider()

            HStack {
                Text("Beleuchtungszonen").font(.subheadline)
                Spacer()
                Button(more ? "Weniger" : "Start/Ruhe/Aus …") { more.toggle() }.buttonStyle(.link).font(.caption)
            }
            ZoneRow(name: "Tastatur", zone: $m.s.keyboard, expanded: more)
            ZoneRow(name: "Lichtleiste", zone: $m.s.lightbar, expanded: more)
            ZoneRow(name: "Logo", zone: $m.s.logo, expanded: more)

            Divider()
            HStack {
                Button("Standard") { m.s = LightingState() }
                Spacer()
                Button("Beenden") { NSApp.terminate(nil) }
            }
        }
        .padding(14)
        .frame(width: 340)
        .onAppear { m.reload() }
    }
}

@main
struct ROGLichtApp: App {
    @StateObject private var model = Model()
    var body: some Scene {
        MenuBarExtra("ROG Licht", systemImage: "keyboard.badge.ellipsis") {
            Panel(m: model)
        }
        .menuBarExtraStyle(.window)
    }
}
