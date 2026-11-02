# Porting `EndfieldHybrid.shader` (Unity URP / HLSL) to Sakuga (Vulkan GLSL)

Source of truth: `reference/EndfieldHybrid.shader`, `reference/EndfieldDebug.shader`.
Port: `shaders/toon.vert|frag`, `shaders/outline.vert|frag`, `shaders/shadow.*`, `shaders/common.glsl`,
`shaders/material_block.glsl` (generated from `engine/gfx/MaterialProps.inc` by `tools/gen_material_glsl.py`).

## Pass mapping
| Unity pass            | Sakuga                                              |
|-----------------------|-----------------------------------------------------|
| `UniversalForward`    | `toon.vert/frag` (`Cull Off`, double-sided via `_DoubleSided`) |
| `Outline` (`Cull Front`, ZWrite On) | `outline.vert/frag`, hull extruded along the world normal, culled on the CPU when width or alpha is 0 |
| `ShadowCaster`        | `shadow.vert/frag`, two texel-snapped directional cascades plus six atlas faces for each of up to four active point lights (desktop: 2048² per cascade, 512² per point face; Android: 1024² and 256² respectively) |
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
* No SSAO, no local reflection probes.
* Shadows *are* implemented since Round 7: two directional cascades sharing one 2x1 atlas (square box fit per cascade, or a
  sphere fit to the camera frustum slice for the near one, both snapped to shadow texels) and a cube atlas for up to two
  point lights. The math lives in `engine/gfx/ShadowFit.h` and is unit tested by `tests/shadow_fit_test.cpp`.
* **Transparency** is implemented: materials with `transparent` (glTF `BLEND`, except hair cards which stay alpha-tested) are drawn after the opaque
  pass with alpha blending, no depth write and no outline, sorted back to front per draw.
* **Face SDF:** for VRM `*Face*SKIN` materials the loader bakes a 1024^2 R/G texture by rasterising the face mesh into UV space (head-local X, G = light from +X, R = light from -X,
  `_FaceSDFDirectionalRG = 1`). The shader's face frame (`_FaceForwardOS` / `_FaceRightOS`) follows the **head bone** (global UBO, `PC.pad0 == 1`), so the shadow follows head turns.
  The shader lab still uses the procedural SDF; hand-made SDF textures can be assigned in the inspector.
* **ZZZ face touches** (three separate, bounded effects - `engine/gfx/FaceShading.h` is the reference implementation and
  `tests/face_hair_test.cpp` pins it; the GLSL block in `shaders/toon.frag` mirrors it):
  1. `faceNoseLighten`: a UV ellipse over the nose/under-eye region releases a fraction of the SDF *cast* shadow, per texel.
  2. `faceNormalFlatten`: for face draws (`PC.pad0 == 1`) the shading normal is lerped toward the view vector, so the light
     map shapes the face instead of the geometric gradient. The geometric normal is still used for the tangent frame/matcap.
  3. `faceAuraStrength` / `faceAuraPower`: an additive rim that only appears toward the silhouette, tinted by the complement
     of the key light. Face materials keep `rimIntensity = 0`, so nothing else adds a rim there.
* **Look tuning for characters** (`GltfLoader.cpp`): `_PBRBaseBlend = 0.4` (NPR-dominant lit side), `_RampThreshold = 0.56` with a narrow feather for crisp cel bands,
  MToon shade colours tinted cool for the shadow ramp, darkened outline colour, warm key light + HDRI sky ambient.
* VRM (MToon) materials are mapped heuristically onto the hybrid parameters (`GltfLoader.cpp`: face / eye / hair name heuristics).
