// Zeichnet Roh-Eingabereports der ASUS N-KEY- und ITE-Geraete auf (nur lesen).
import Foundation
import IOKit.hid
let out = FileHandle(forWritingAtPath: CommandLine.arguments[1]) ?? FileHandle.standardOutput
func log(_ s: String) {
    let line = "\(String(format: "%.3f", Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 1000))) \(s)\n"
    out.write(line.data(using: .utf8)!); print(s); fflush(stdout)
}
let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatchingMultiple(mgr, [[kIOHIDVendorIDKey: 0x0b05, kIOHIDProductIDKey: 0x19b6],
                                            [kIOHIDVendorIDKey: 0x0b05, kIOHIDProductIDKey: 0x193b]] as CFArray)
IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
let r = IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
log("IOHIDManagerOpen = \(String(format: "0x%x", r))")
guard let devs = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice> else { log("keine Geraete"); exit(1) }
var buffers: [UnsafeMutablePointer<UInt8>] = []
for d in devs {
    let name = IOHIDDeviceGetProperty(d, kIOHIDProductKey as CFString) as? String ?? "?"
    log("Geraet: \(name)")
    let buf = UnsafeMutablePointer<UInt8>.allocate(capacity: 128); buffers.append(buf)
    let ctx = Unmanaged.passRetained(name as NSString).toOpaque()
    IOHIDDeviceRegisterInputReportCallback(d, buf, 128, { ctx, res, sender, type, id, report, len in
        let name = Unmanaged<NSString>.fromOpaque(ctx!).takeUnretainedValue() as String
        let bytes = (0..<min(len, 16)).map { String(format: "%02x", report[$0]) }.joined(separator: " ")
        if id == 1 || id == 0 { return }       // normale Tastatur-/Maus-Reports ueberspringen
        log("\(name) report=0x\(String(id, radix: 16)) len=\(len): \(bytes)")
    }, ctx)
}
log("Bereit. Tasten druecken, Ende mit Ctrl-C.")
CFRunLoopRun()
