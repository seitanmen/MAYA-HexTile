# MAYA-HexTile

Maya native plugin implementing **stochastic hex-tiling** to reduce visible repetition in tileable textures.

## Overview

Based on "Practical Real-Time Hex-Tiling" by Morten S. Mikkelsen (JCGT 2022). This plugin provides a Maya HyperShade-native shading node with VP2 (Viewport 2.0) rendering support, plus Redshift OSL and Arnold shader implementations.

> **Status:** Proof of Concept. The DG node and VP2 override are verified headless; viewport/swatch rendering requires GUI validation. The node class is currently `VMTHexTilePoc` (the `MTypeId 0x0007F140` is an internal test ID and must be reassigned before distribution).

## Features

- **VMTHexTilePoc**: Maya shading node (`texture/2d` classification) with hex-based stochastic sampling
- **8 fixed color slots**: `colorMap0`..`colorMap7` inputs → `outColor0`..`outColor7` outputs
- **VP2 Support**: Viewport rendering via dynamically built shade fragments (`FragmentBuilder`)
- **Redshift OSL**: `osl/vmtHexTile.osl` (+ compiled `.oso`)
- **Arnold**: `src/arnold/VMTHexTileArnold.cpp` (experimental)

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

In HyperShade: **Create → Texture → VMTHexTilePoc**, then connect `file` nodes into `colorMap0`..`colorMap7` and wire `outColor0` (etc.) into a material.

Headless smoke tests live in `test/` (run with `mayapy`).

### Redshift OSL

OSL shader: `osl/vmtHexTile.osl` (compiled `osl/vmtHexTile.oso`, Redshift variant `osl/vmtHexTile_rs.oso`).
Deploy via `scripts/vmtHexToRedshift.py` (resolves the OSL path relative to the script).

## Parameters

| Attribute (long) | Short | Type | Description | Default |
|------------------|-------|------|-------------|---------|
| `colorMap0`..`colorMap7` | — | color | Input texture maps (8 fixed slots) | — |
| `tileScale` | `tsc` | float | Hex grid density | 2.0 |
| `rotStrength` | `rot` | float | Rotation randomness | 0.5 |
| `tileBlend` | `tbl` | float | Blend contrast | 0.5 |
| `heightMap` | — | color | Optional height input | — |
| `heightWeight` | `hwt` | float | Height blend influence | 1.0 |
| `heightDelta` | `hdl` | float | Height transition width | 0.2 |
| `normalConvention` | `ncv` | enum | OpenGL / DirectX | 0 |
| `outColor0`..`outColor7` | — | color | Hex-tiled outputs (unconnected = black) | — |

## Algorithm Details

- **TriangleGrid**: Pixel → 3 hex vertices with random offsets/rotations
- **Hash**: `frac(sin(dot)*43758.5453)` per-vertex (ported from the Nuke version; see cross-platform note below)
- **Sampling**: gradient-based filtering with UV wrapping
- **Blending**:
  - Luminance-based: barycentric weight^γ × luminance diffusion (Rec.601)
  - Height-based: soft threshold transition

> **PoC note:** the DG node's `compute()` currently passes `colorMap{i}` straight through to `outColor{i}`; the actual hex compositing runs in the VP2 shade fragment (and in the OSL/Arnold shaders). DG-only renderers will not show the hex-tiling effect yet.

> **Cross-platform note:** the `sin`-based hash is not guaranteed bit-identical across CPU/GPU/OSL. An integer-hash (murmur3) replacement is proposed in the workspace docs for exact Nuke/Arnold/Redshift/VP2 agreement but is **not yet applied**.

## Requirements

- Visual Studio 2022 (MSVC v143)
- CMake 4.x
- Maya 2026 (or 2023 with `-DMAYA_LOCATION` pointing at the 2023 install)
- Redshift (for OSL rendering)
- Windows only

## License

MPL-2.0 (Mozilla Public License 2.0) — See [LICENSE](LICENSE)

## Citation

This implementation is based on:

```
Morten S. Mikkelsen, "Practical Real-Time Hex-Tiling",
Journal of Computer Graphics Techniques, vol. 11, no. 2, pp. 77-94, 2022.
```

Paper: https://jcgt.org/published/0011/03/05/

Reference implementation: https://github.com/mmikk/hextile-demo (MIT License)

## Author

Seitanmen — https://github.com/seitanmen

## Documentation

Design specs and implementation notes are maintained in the workspace `docs/` folder
(outside this repository): `02_Mayaノード規格.md`, `03_Maya実装計画.md`,
`01_Maya移植_調査ナレッジ.md`.
