# AnimeSky post-processing

## Current Vulkan implementation

`engine/post/PostProcessing` owns the descriptor sets, full-screen graphics pipelines and intermediate HDR images independently of the scene renderer. The caller supplies the resolved linear RGBA16F scene and LUT, records the passes on the graphics queue, and receives an RGBA8 display-encoded image. Scene rendering includes sky and clouds, so bright sun, cloud and emissive pixels feed the same prefilter as other bright pixels. ImGui is composited afterwards. This is a scene-wide pipeline, not a baked sky texture.

1. Resolved HDR scene → soft-knee brightness prefilter (half resolution, Karis weighting).
2. Multilevel HDR bloom downsample and additive upsample.
3. **Optional separate halation pass:** wider quarter-resolution diffusion of the bright bloom buffer, composited with a red/orange bias in HDR. It is skipped unless bloom and halation are enabled; the small intermediate remains allocated. This is an artistic approximation, not a physically simulated film emulsion or individually masked lights.
4. HDR bloom/halation composite → exposure → neutral/ACES/clamp tonemap → linear split-tone and saturation → sRGB encoding → display contrast and optional 3D LUT → vignette, grain and display dithering.
5. UI and presentation follow. Depth fog currently occurs per surface in toon/outline shaders, **not** in a screen-space post pass. The scene sky shader has its own gradient and sun/cloud rendering.

The module rebuilds transient images/descriptors on resize or LUT replacement after the caller waits for in-flight frames. It uses dynamic rendering and explicit image barriers on the existing graphics queue, not async compute. Any future pass must document its image format, resolution, layout transitions, lifetime, inputs/outputs and ordering (linear HDR vs display-referred); do not sample an image while writing it. On Android the `Glow` control sets halation when bloom is on. In the full ImGui editor open **Post-processing (HDR)** for four creative presets and individual controls. Presets are independent of the sky palette and preserve exposure, enabled state, and the selected LUT/strength; further slider edits are allowed. Current defaults remain unchanged unless a preset is selected.

## Scope and next steps

The four presets are small tunable starting points, not reconstructions of Genshin, Honkai, Wuthering Waves or Wind Waker. They are not yet artist-calibrated on target devices. The threshold is brightness-based: there is no emissive/object selection mask. There is no depth-resolved aerial perspective pass, physical atmosphere or volumetric clouds, lens flare, chromatic aberration, depth of field, screen-space outline enhancement, colour quantization or painterly filter. Existing geometry outlines, per-surface exponential fog, LUT grading, grain, vignette and display dither should not be confused with those missing effects. A proper atmospheric pass needs sampleable resolved depth, camera/projection data and sky/transparent handling; avoid applying it again on top of the existing fog without reconciling the two paths.

No GPU timestamp profiling or device-specific performance/visual validation has been added here. Quarter-resolution halation and the existing bloom chain add GPU bandwidth; test with bloom/halation both off and on, particularly on Intel UHD and phones, before enabling more effects. The Vulkan emulator smoke checks presentation and the mobile control, not image quality or real-hardware timing. CPU tests verify profile values and pass selection without a GPU. Shader sources and their precompiled SPIR-V are both committed.

The [reference collection](../reference/AnimeSky_Reference_Images/README.md) is an external-link index, not a redistributable image pack.
