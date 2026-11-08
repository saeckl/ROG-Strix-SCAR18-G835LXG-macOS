import Metal
import Foundation
let src = """
#include <metal_stdlib>
using namespace metal;
kernel void k(device float *out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
  float a = float(id) * 1e-7f, b = 1.0001f, c = 0.9999f, d = a + 1.0f, e = a + 2.0f, f = a + 3.0f, g = a + 4.0f;
  for (int i = 0; i < 4096; i++) {
    a = fma(a, b, c); d = fma(d, b, c); e = fma(e, b, c); f = fma(f, b, c); g = fma(g, b, c);
    a = fma(a, c, b); d = fma(d, c, b); e = fma(e, c, b); f = fma(f, c, b); g = fma(g, c, b);
  }
  out[id] = a + d + e + f + g;
}
"""
let dev = MTLCreateSystemDefaultDevice()!
print("Geraet:", dev.name)
let lib = try! dev.makeLibrary(source: src, options: nil)
let ps = try! dev.makeComputePipelineState(function: lib.makeFunction(name: "k")!)
let n = 82 * 128 * 512
let buf = dev.makeBuffer(length: n * 4, options: .storageModePrivate)!
let q = dev.makeCommandQueue()!
func run() -> Double {
  let cb = q.makeCommandBuffer()!; let e = cb.makeComputeCommandEncoder()!
  e.setComputePipelineState(ps); e.setBuffer(buf, offset: 0, index: 0)
  e.dispatchThreads(MTLSize(width: n, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 256, height: 1, depth: 1))
  e.endEncoding(); let t = Date(); cb.commit(); cb.waitUntilCompleted(); return Date().timeIntervalSince(t)
}
_ = run()
let start = Date(); var sec = 0; var acc: [Double] = []
while Date().timeIntervalSince(start) < 15 {
  let t = run(); acc.append(t)
  let el = Int(Date().timeIntervalSince(start))
  if el > sec { sec = el
    let best = acc.min()!, avg = acc.reduce(0,+)/Double(acc.count)
    let f = Double(n) * 4096 * 10 * 2
    print(String(format: "s%2d: Mittel %.1f TFLOPS (%.0f MHz), bestes %.1f", el, f/avg/1e12, f/avg/(82*128*2)/1e6, f/best/1e12)); acc = [] }
}
