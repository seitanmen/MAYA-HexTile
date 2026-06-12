# MAYA-HexTile

Maya native plugin implementing **stochastic hex-tiling** to reduce visible repetition in tileable textures.

## Overview

Based on "Practical Real-Time Hex-Tiling" by Morten S. Mikkelsen (JCGT 2022). This plugin provides a Maya HyperShade-native shading node that renders the hex-tiling effect directly in Viewport 2.0 / GUI, plus Arnold (native node translation) and Redshift (OSL) support.

> **Node ID note:** the node class is currently `VMTHexTilePoc` and uses `MTypeId 0x0007F140` (an internal test ID). Both should be finalized before public distribution.

## Features

- **VMTHexTilePoc**: Maya shading node (classification `texture/2d:drawdb/shader/texture/2d/VMTHexTilePoc`) with hex-based stochastic sampling
- **8 fixed color slots**: `colorMap0`..`colorMap7` inputs → `outColor0`..`outColor7` outputs, plus an optional `normalMap` input → `outNormal`
- **VP2 / GUI rendering**: the node renders natively in Viewport 2.0. The VP2 override loads textures via `MTextureManager` and builds shade fragments dynamically (`FragmentBuilder`), so the hex effect is visible in the interactive viewport and swatches.
- **Arnold**: renders via MtoA node translation without replacing the node (see [Arnold](#arnold)).
- **Redshift (OSL)**: no public translation SDK, so a one-click script generates a `RedshiftOSLShader` network (see [Redshift (OSL)](#redshift-osl)).

## Build (Windows / Maya 2026)

```bat
cd maya_hextile
cmake -G "Visual Studio 17 2022" -A x64 -B build ^
      -DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2026"
cmake --build build --config Release
```

Output: `build/Release/VMTHexTilePoc.mll`

> The output base name can be changed with `-DPLUGIN_OUTPUT_NAME=...` (useful when Maya has an older `.mll` loaded and locked).

## Load & Test

```mel
loadPlugin "<repo>/maya_hextile/build/Release/VMTHexTilePoc.mll";
```

In HyperShade: **Create → Texture → VMTHexTilePoc**, then connect `file` nodes into `colorMap0`..`colorMap7` and wire `outColor0` (etc.) into a material. The hex-tiling result is shown directly in Viewport 2.0.

Headless smoke tests live in `test/` (run with `mayapy`).

### Arnold

Renders through the MtoA node translator (`src/arnold/`) without replacing the node. Connect the node into an Arnold material. Deploy step: place `osl/vmtHexTile.oso` on `ARNOLD_PLUGIN_PATH` (Arnold reads it as the `vmtHexTile` node type) and the built extension `.dll` on `MTOA_EXTENSIONS_PATH` (registers the translator).

### Redshift (OSL)

Redshift does not expose a public node-translation SDK, so the node cannot be auto-translated. Use the one-click converter to generate a `RedshiftOSLShader` network instead:

```python
import sys; sys.path.append(r"<repo>/maya_hextile/scripts")
import vmtHexToRedshift as v; v.run()   # converts the selected VMTHexTilePoc
```

It loads `osl/vmtHexTile.osl` (resolved relative to the script), transfers `tileScale`/`rotStrength`/`tileBlend`/height/normal, and pulls each connected `file` path + colorSpace into `colorMap{i}`/`srgb{i}`. Connect the generated `outColor0`..`outColor7` to a Redshift material. The OSL is compiled ahead of time to `osl/vmtHexTile.oso` (the repo also ships a `osl/vmtHexTile_rs.oso` build); recompile with `oslc` if you edit the `.osl`.

## Parameters

| Attribute (long) | Short | Type | Description | Default |
|------------------|-------|------|-------------|---------|
| `colorMap0`..`colorMap7` | `cm0`..`cm7` | color | Input texture maps (8 fixed slots) | — |
| `tileScale` | `tsc` | float | Hex grid density | 2.0 |
| `rotStrength` | `rot` | float | Rotation randomness | 0.5 |
| `tileBlend` | `tbl` | float | Blend contrast | 0.5 |
| `heightMap` | `hm` | color | Optional height input | — |
| `heightWeight` | `hwt` | float | Height blend influence | 1.0 |
| `heightDelta` | `hdl` | float | Height transition width | 0.2 |
| `normalMap` | `nm` | color | Optional tangent-space normal input | — |
| `normalConvention` | `ncv` | enum | OpenGL (0) / DirectX (1) | 0 |
| `outColor0`..`outColor7` | `ocl0`..`ocl7` | color | Hex-tiled color outputs (unconnected = black) | — |
| `outColor` | `ocl` | color | Standard swatch/color output | — |
| `outNormal` | `onr` | color | Hex-tiled normal output | — |

## Algorithm Details

- **TriangleGrid**: Pixel → 3 hex vertices with random offsets/rotations
- **Hash**: murmur3-style integer hash (`fmix32`) — see note below
- **Sampling**: gradient-based filtering (`SampleGrad`) with UV wrapping
- **Blending**:
  - Luminance-based: barycentric weight^γ × luminance diffusion (Rec.601)
  - Height-based: soft threshold transition

> **Where the hex compositing runs:** the effect is implemented per-renderer — the VP2 shade fragment (interactive viewport), the Arnold node translator, and the OSL shader. The DG node's `compute()` is only a CPU fallback for software swatches/soft renders (color slots pass through; alpha/height/normal return defaults); it is not the rendering path.

> **Hash note:** the active rendering paths — the dynamically built VP2 fragment (`FragmentBuilder::hlslHelpers`) and the OSL shader (`vmtHexTile.osl`) — use an integer murmur3 `fmix32` hash, chosen for consistent results across CPU/GPU/OSL. An older `frac(sin(dot)*43758.5453)` hash remains only in the legacy static-fragment path (`hlslSource`/`glslSource`), which the override does not select.

## Requirements

- Visual Studio 2022 (MSVC v143)
- CMake 3.16+
- Maya 2026 (or 2023 with `-DMAYA_LOCATION` pointing at the 2023 install)
- Redshift (for OSL rendering)
- Windows only

## License

MPL-2.0 (Mozilla Public License 2.0) — See [LICENSE](LICENSE)

## References

This implementation is based directly on Mikkelsen's hex-tiling method, which is itself an
adaptation of the earlier by-example tiling-and-blending work by Heitz, Neyret and Deliot.

1. Morten S. Mikkelsen. "Practical Real-Time Hex-Tiling." *Journal of Computer Graphics
   Techniques (JCGT)*, vol. 11, no. 2, pp. 77–94, 2022.
   https://jcgt.org/published/0011/03/05/ — reference implementation:
   https://github.com/mmikk/hextile-demo (MIT License)

2. Eric Heitz and Fabrice Neyret. "High-Performance By-Example Noise using a
   Histogram-Preserving Blending Operator." *Proceedings of the ACM on Computer Graphics and
   Interactive Techniques (ACM SIGGRAPH / Eurographics High-Performance Graphics)*, vol. 1,
   no. 2, art. 31, pp. 1–25, 2018. doi:10.1145/3233304

3. Thomas Deliot and Eric Heitz. "Procedural Stochastic Textures by Tiling and Blending."
   In *GPU Zen 2*, Wolfgang Engel (ed.), Black Cat Publishing, 2019.

4. Brent Burley. "On Histogram-Preserving Blending for Randomized Texture Tiling."
   *Journal of Computer Graphics Techniques (JCGT)*, vol. 8, no. 4, pp. 31–53, 2019.
   https://jcgt.org/published/0008/04/02/

## Author

Seitanmen — https://github.com/seitanmen
