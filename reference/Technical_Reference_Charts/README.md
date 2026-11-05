# Original rendering reference charts

These four self-contained SVG illustrations are **technical schematics**, not screenshots or
art-style targets. They were created for this repository using only basic SVG shapes, colours and
generic system-font references; they contain no third-party artwork, fonts or image assets.
They are dedicated under [CC0 1.0](LICENSE.md). The dedication applies only to the files in
this directory, not to the engine, LAFAN1, external links, or other assets.

| File | What it illustrates | Use |
| --- | --- | --- |
| `01-emissive-selection.svg` | Reflected light versus emitted radiance | Inspect the selected HDR source before threshold and bloom. |
| `02-transparency.svg` | Source-over alpha and opaque depth | Check layer ordering; the current fog/DoF path is not per-layer correct. |
| `03-geometry-edges.svg` | Depth silhouettes and reconstructed-normal changes | Compare silhouette and normal-edge behaviour, not exact pixel values. |
| `04-sun-occlusion.svg` | Visible versus occluded sun | A target for a future occlusion-aware flare; the current lens ghosts do not meet it. |

They are illustrative, display-referred RGB diagrams: glow and colours are not calibrated HDR
radiometry, physically exact images, or ground truth for a particular GPU or monitor. They do not
replace artist-selected, rights-cleared style references, real-device evaluation, or quantitative
measurements. Open an SVG locally in a browser or editor; no network access is required.
