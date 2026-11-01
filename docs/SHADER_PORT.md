# Porting `EndfieldHybrid.shader` (Unity URP / HLSL) to Sakuga (Vulkan GLSL)

Source of truth: `reference/EndfieldHybrid.shader`, `reference/EndfieldDebug.shader`.
Port: `shaders/toon.vert|frag`, `shaders/outline.vert|frag`, `shaders/shadow.*`, `shaders/common.glsl`,
`shaders/material_block.glsl` (generated from `engine/gfx/MaterialProps.inc` by `tools/gen_material_glsl.py`).

## Pass mapping
| Unity pass            | Sakuga                                              |
|-----------------------|-----------------------------------------------------|
| `UniversalForward`    | `toon.vert/frag` (`Cull Off`, double-sided via `_DoubleSided`) |
| `Outline` (`Cull Front`, ZWrite On) | `outline.vert/frag`, hull extruded along the world normal, culled on the CPU when width or alpha is 0 |
| `ShadowCaster`        | `shadow.vert/frag`, 2048² D32 ortho map, texel snapped |
| `DepthNormals`        | not needed (no SSAO/depth-normal consumers yet)     |
| `EndfieldDebug`       | folded into `toon.frag` as debug modes 1-13 (+14-17 extras: PBR, LUT, NPR-lit, PBR params) |

## Properties
Every `_Property` becomes a field of `MaterialParams` (`MaterialProps.inc` is the single list; the C++ struct,
the GLSL UBO block and the ImGui inspector are all generated from it with X-macros). Textures use fixed slots
(`TexSlot`: Base, Normal, Param, Style, Ramp, SpecRamp, LUT, MatCap, Mask, FaceSDF, Highlight, HairSpec, Emission, Flow).

## HLSL -> GLSL gotchas that mattered
* `frac`->`fract`, `saturate(x)`->`clamp(x,0,1)`, `lerp`->`mix`, `mul(M,v)` argument order, `clip(x)` -> `if (x < 0.0) discard;`.
* Reserved GLSL identifiers (`active`, `half`, `fixed`, `sample`, `filter`, `input`, `output`) were renamed.
* Vulkan clip space has +Y down: the projection Y is flipped (`Renderer::perspective`), `gl_FragCoord` already has a top-left origin
  (the split-screen line therefore needs no `_ProjectionParams.x` fix), and the MatCap lookup uses `-normalVS.y`.
  With the flipped projection the *front* face winding of glTF (CCW) is `VK_FRONT_FACE_COUNTER_CLOCKWISE` (checked empirically).
* URP lighting helpers are re-implemented: main-light shadow (3x3 PCF, hardware compare), simplified BRDF
  (`universalFragmentPBR`), main light + 4 additional point lights (no shadows), and **image based lighting**: 9-coefficient SH ambient and a
  roughness-selected mip of an equirect map for reflections (`--env file.hdr`; a procedural sky map is generated when no HDRI is loaded).
* Output is encoded to sRGB manually (`encodeOutput`) because all render targets are UNORM; optional ACES.
* **Lookup textures and derivatives:** ramps / LUT / MatCap are sampled with coordinates that are constant over large areas
  (zero screen-space derivatives). With anisotropic filtering on, some implementations (llvmpipe) return NaN there.
  Sakuga therefore enables anisotropy only on mip-mapped textures and samples the MatCap with `textureLod`.
* The robust tangent frame in `toon.frag` handles meshes without tangents (Fox, CesiumMan) - normalising a zero tangent gives NaN.

## Not ported / differences
* No SSAO, no local reflection probes, no cascaded shadow maps, point lights are unshadowed.
* **Transparency** is implemented: materials with `transparent` (glTF `BLEND`, except hair cards which stay alpha-tested) are drawn after the opaque
  pass with alpha blending, no depth write and no outline, sorted back to front per draw.
* **Face SDF:** for VRM `*Face*SKIN` materials the loader bakes a 1024^2 R/G texture by rasterising the face mesh into UV space (head-local X, G = light from +X, R = light from -X,
  `_FaceSDFDirectionalRG = 1`). The shader's face frame (`_FaceForwardOS` / `_FaceRightOS`) follows the **head bone** (global UBO, `PC.pad0 == 1`), so the shadow follows head turns.
  The shader lab still uses the procedural SDF; hand-made SDF textures can be assigned in the inspector.
* **Look tuning for characters** (`GltfLoader.cpp`): `_PBRBaseBlend = 0.4` (NPR-dominant lit side), `_RampThreshold = 0.56` with a narrow feather for crisp cel bands,
  MToon shade colours tinted cool for the shadow ramp, darkened outline colour, warm key light + HDRI sky ambient.
* VRM (MToon) materials are mapped heuristically onto the hybrid parameters (`GltfLoader.cpp`: face / eye / hair name heuristics).
