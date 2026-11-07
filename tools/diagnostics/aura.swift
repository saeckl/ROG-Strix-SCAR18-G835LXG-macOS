// Sendet ASUS-Aura-Feature-Reports an die N-KEY-Tastatur (0b05:19b6).
// Aufruf: aura <hex bytes...>   z. B. aura 5a ba c5 c4 03
import Foundation
import IOKit.hid
let mgr = IOHIDManagerCreate(kCFAllocatorDefault, 0)
IOHIDManagerSetDeviceMatching(mgr, [kIOHIDVendorIDKey: 0x0b05, kIOHIDProductIDKey: 0x19b6] as CFDictionary)
guard IOHIDManagerOpen(mgr, 0) == kIOReturnSuccess,
      let dev = (IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>)?.first else { print("N-KEY nicht gefunden/geoeffnet"); exit(1) }
var bytes = CommandLine.arguments.dropFirst().map { UInt8($0, radix: 16)! }
let id = bytes[0]
while bytes.count < 64 { bytes.append(0) }
let r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeFeature, CFIndex(id), bytes, bytes.count)
print(String(format: "SetReport 0x%02x -> 0x%x", id, r))
exit(r == kIOReturnSuccess ? 0 : 1)
