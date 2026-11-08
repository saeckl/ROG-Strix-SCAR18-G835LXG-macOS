# NAK panic in World of Warcraft compute kernels (NullMoth 2.7.2, RTX 5090 Laptop / GB203)

## Status

Not reproducible with NullMoth 2.8 (8 Nov 2026): both kernels, including the SAFE variant, compile with
2.8's NVK with predload on (443/443 cached kernels OK, shader cache disabled), and WoW runs without
`NVMTL_NAK_NO_PREDLOAD=1`. The predload code itself is unchanged in 2.8, so the patch below is still a valid
hardening, but no longer needed for WoW.

## Symptom

World of Warcraft (Metal) shows a black screen; `gx.log`: `newComputePipelineState: vkCreateComputePipelines
FAILED` (Prism), then `Device context was lost` in a loop. NVK reports
`nvk_shader.c:627: Internal compiler error in NAK (VK_ERROR_UNKNOWN)`; stderr of the game:

```
panicked at ../src/nouveau/compiler/nak/from_nir.rs:3346:17:
assertion failed: u32::from(size_B) <= intrin.align()
panicked at ../src/nouveau/compiler/nak/ir.rs:2328:18:
Invalid memory load/store size
```

## Cause

`nak_cg_predload` (speed267, `nak_predload.diff`) turns `load_global`/`load_global_constant` into
`load_global_nv`. It runs after `nir_opt_load_store_vectorize` — whose NAK callback combines optimistically
("we legalize these later") — but before `nir_lower_mem_access_bit_sizes`, which does not split
`load_global_nv`. So an unlegalized load reaches `from_nir`:

- the align guard's SAFE module has vec4 loads with `Aligned 4` → 16 B load, align 4 → assertion;
- in another kernel three adjacent f32 loads are vectorized to a vec3 → 12 B → invalid size.

## Evidence

`nvkc.c` compiles SPIR-V with `/Library/GPUBundles/nvmtl/libvulkan_nouveau.dylib` directly;
`lower.c` runs the translator's `nvmtl_lower_buffer_addresses_ex` (flags 0 = normal, 1 = SAFE).
With `MESA_SHADER_CACHE_DISABLE=true` (the cache key does not include the NVMTL_NAK_* switches,
so a cached result hides the bug):

| module | default | `NVMTL_NAK_NO_PREDLOAD=1` |
| --- | --- | --- |
| kernel f9abb4a2, SAFE (flags 1) | 15/15 panic (align) | 0/15 |
| kernel bed606c1, flags 0 | 15/15 panic (size) | 0/15 |
| kernel f9abb4a2, flags 0 | 0/15 | 0/15 |

In the game with `NVMTL_NAK_NO_PREDLOAD=1`: 0 NAK panics, 0 failed pipelines, no device loss in 60 s
(before: 89–236 panics and a device-lost loop).

## Fix

`0001-nak-predload-only-hoist-legal-ldg.patch`: `nak_cg_predload_side` only accepts a load that is already
one legal LDG (1/2/4/8/16 bytes, size <= combined alignment); other sides are left as branches. The rest of
predload is unchanged. Not built here (the public tree does not contain the full 2.7.2 NVK sources); the
logic was checked against the failing modules above.

Workaround until then: `launchctl setenv NVMTL_NAK_NO_PREDLOAD 1` (the behaviour before speed267).

Side note: Mesa's shader cache key ignores the NVMTL_NAK_* environment switches, so results built with
one setting are served to another.

The patch changes code from the NullMoth NVIDIA driver (PolyForm Noncommercial 1.0.0,
https://polyformproject.org/licenses/noncommercial/1.0.0, https://github.com/nullmoth/nvidia-macos-driver) and Mesa (MIT).
No game shaders are included.
