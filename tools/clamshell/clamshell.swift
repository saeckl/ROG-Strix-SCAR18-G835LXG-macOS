// clamshell-agent – lid-closed mode with an external display on the G835LXG.
//
// macOS notices the lid (IOPMrootDomain "AppleClamshellState"), but only switches the built-in panel off
// for an AppleBacklightDisplay. NullMoth's framebuffer has no backlight control, so the panel is a plain
// AppleDisplay and stays on. This agent does the switch itself:
//   lid closed + external display online -> external display becomes main, built-in panel is switched off
//   lid open (or no external display)    -> built-in panel back on and main again
// Configurations are applied "for session" only, so a reboot or logout always brings the panel back.
import CoreGraphics
import Foundation
import IOKit

@_silgen_name("CGSConfigureDisplayEnabled")
func CGSConfigureDisplayEnabled(_ c: CGDisplayConfigRef, _ d: CGDirectDisplayID, _ e: Bool) -> CGError

func log(_ s: String) {
    let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HH:mm:ss"
    print("\(f.string(from: Date())) \(s)"); fflush(stdout)
}

func displays() -> [CGDirectDisplayID] {
    var n: UInt32 = 0; CGGetOnlineDisplayList(0, nil, &n)
    var a = [CGDirectDisplayID](repeating: 0, count: Int(n)); CGGetOnlineDisplayList(n, &a, &n)
    return Array(a.prefix(Int(n)))
}

func lidClosed() -> Bool? {
    let root = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPMrootDomain"))
    guard root != 0 else { return nil }
    defer { IOObjectRelease(root) }
    let v = IORegistryEntryCreateCFProperty(root, "AppleClamshellState" as CFString, kCFAllocatorDefault, 0)
    return (v?.takeRetainedValue() as? Bool)
}

func makeMain(_ id: CGDirectDisplayID) -> Bool {
    let b = CGDisplayBounds(id)
    if b.origin == .zero { return true }
    var c: CGDisplayConfigRef?
    guard CGBeginDisplayConfiguration(&c) == .success else { return false }
    for d in displays() where CGDisplayIsActive(d) != 0 {
        let o = CGDisplayBounds(d).origin
        CGConfigureDisplayOrigin(c, d, Int32(o.x - b.origin.x), Int32(o.y - b.origin.y))
    }
    return CGCompleteDisplayConfiguration(c, .forSession) == .success
}

func setEnabled(_ id: CGDirectDisplayID, _ on: Bool) -> Bool {
    var c: CGDisplayConfigRef?
    guard CGBeginDisplayConfiguration(&c) == .success, let cfg = c else { return false }
    let r = CGSConfigureDisplayEnabled(cfg, id, on)
    guard r == .success else { CGCancelDisplayConfiguration(cfg); return false }
    return CGCompleteDisplayConfiguration(cfg, .forSession) == .success
}

var weTurnedOff: CGDirectDisplayID? = nil

func tick() {
    let all = displays()
    guard let builtin = all.first(where: { CGDisplayIsBuiltin($0) != 0 }) else { return }
    let externals = all.filter { CGDisplayIsBuiltin($0) == 0 && CGDisplayIsActive($0) != 0 }
    let closed = lidClosed() ?? false
    let builtinActive = CGDisplayIsActive(builtin) != 0

    if closed, let ext = externals.first {
        if builtinActive {
            let m = makeMain(ext)
            let off = setEnabled(builtin, false)
            if off { weTurnedOff = builtin }
            log("lid closed: external \(ext) main=\(m), built-in \(builtin) off=\(off)")
        }
    } else if !builtinActive {
        // lid open, or the external display went away while closed: never leave the Mac without a screen
        let on = setEnabled(builtin, true)
        Thread.sleep(forTimeInterval: 1.5)
        let m = makeMain(builtin)
        weTurnedOff = nil
        log("\(closed ? "no external display" : "lid open"): built-in \(builtin) on=\(on) main=\(m)")
    }
}

signal(SIGTERM) { _ in
    // leave the panel on when the agent stops
    if let b = displays().first(where: { CGDisplayIsBuiltin($0) != 0 }), CGDisplayIsActive(b) == 0 {
        _ = setEnabled(b, true); _ = makeMain(b)
    }
    exit(0)
}

log("clamshell-agent started (lid \(lidClosed() == true ? "closed" : "open"), \(displays().count) display(s))")
let t = Timer(timeInterval: 1.0, repeats: true) { _ in tick() }
RunLoop.main.add(t, forMode: .common)
tick()
RunLoop.main.run()
