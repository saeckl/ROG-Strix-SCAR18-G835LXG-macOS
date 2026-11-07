import Metal
import Foundation
let src = """
#include <metal_stdlib>
using namespace metal;
kernel void k(device float *out [[buffer(0)]], constant uint &n [[buffer(1)]], uint id [[thread_position_in_grid]]) {
  float a = float(id) * 1e-7f + 0.5f, b = out[1], c = out[2];
  for (uint i = 0; i < n; i++) { a = fma(a, b, c); a = fma(a, b, c); a = fma(a, b, c); a = fma(a, b, c); }
  out[id + 4] = a;
}
"""
let dev = MTLCreateSystemDefaultDevice()!
let lib = try! dev.makeLibrary(source: src, options: nil)
let ps = try! dev.makeComputePipelineState(function: lib.makeFunction(name: "k")!)
let buf = dev.makeBuffer(length: 4096, options: .storageModeShared)!
let p = buf.contents().bindMemory(to: Float.self, capacity: 1024); p[1] = 0.99999; p[2] = 0.00001
let q = dev.makeCommandQueue()!
func run(_ n: UInt32) -> Double {
  var nn = n
  let cb = q.makeCommandBuffer()!; let e = cb.makeComputeCommandEncoder()!
  e.setComputePipelineState(ps); e.setBuffer(buf, offset: 0, index: 0); e.setBytes(&nn, length: 4, index: 1)
  e.dispatchThreads(MTLSize(width: 1, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 1, height: 1, depth: 1))
  e.endEncoding(); cb.commit(); cb.waitUntilCompleted(); return cb.gpuEndTime - cb.gpuStartTime > 0 ? cb.gpuEndTime - cb.gpuStartTime : -1
}
func wall(_ n: UInt32) -> Double { let t = Date(); _ = run(n); return Date().timeIntervalSince(t) }
_ = wall(1000)
let small = (0..<5).map { _ in wall(100_000) }.min()!
let big = (0..<5).map { _ in wall(2_100_000) }.min()!
let fmas = Double(2_000_000) * 4
let dt = big - small
print(String(format: "Differenz %.1f ms fuer %.0f abhaengige FMA -> %.0f MHz bei 4 Takten Latenz", dt * 1000, fmas, fmas * 4 / dt / 1e6))
