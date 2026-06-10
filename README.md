# MAYA-HexTile

Maya native plugin implementing **stochastic hex-tiling** to reduce visible repetition in tileable textures.

## Overview

Based on "Practical Real-Time Hex-Tiling" by Morten S. Mikkelsen (JCGT 2022). This plugin provides Maya HyperShade-native nodes with VP2 (Viewport 2.0) rendering support and Redshift OSL integration.

## Features

- **VMTHexTile**: Maya shading node with hex-based stochastic sampling
- **VP2 Support**: Real-time viewport rendering via custom fragments
- **Redshift OSL**: Redshift rendering with OSL shader
- **Arnold Support**: Planned (Phase 5)

## Installation

### Build from Source

```bash
cd maya_hextile
cmake -G "Visual Studio 17 2022" -A x64 -B build ^
      -DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2026"
cmake --build build --config Release
```

Output: `build/Release/VMTHexTile.mll`

### Maya Module Setup

Create `C:/Users/<user>/Documents/maya/2026/modules/VMTHexTile.mod`:
```
+ VMTHexTile 1.0 .
```

Add to MAYA_PLUGIN_PATH:
```python
import os
os.environ['MAYA_PLUGIN_PATH'] = os.path.join(os.path.dirname(__file__), 'maya_hextile', 'build', 'Release')
```

### Redshift OSL Deployment

OSL shader: `osl/vmtHexTile.osl`

Deploy to Redshift OSL path or set `OSL_SHADER_PATHS` environment variable.

## Usage

### Load Plugin

```mel
loadPlugin("<repo>/maya_hextile/build/Release/VMTHexTile.mll");
```

### Create Node in HyperShade

1. Open HyperShade
2. Create → Texture → VMTHexTile
3. Connect texture maps to `colorMaps[]` array
4. Connect output to material

### Parameters

| Attribute | Type | Description | Default |
|-----------|------|-------------|---------|
| `colorMaps[]` | array | Input texture maps (1-N) | empty |
| `tileScale` | float | Hex grid density | 1.0 |
| `rotStrength` | float | Rotation randomness | 0.5 |
| `tileBlend` | float | Barycentric weight exponent | 0.5 |
| `heightWeight` | float | Height blend influence | 1.0 |
| `heightDelta` | float | Height transition width | 0.2 |

## Algorithm Details

- **TriangleGrid**: Pixel → 3 hex vertices with random offsets/rotations
- **Hash**: Per-vertex randomness (murmur3-based for cross-platform consistency)
- **Sampling**: Gradient-based filtering with UV wrapping
- **Blending**: 
  - Luminance-based: Barycentric weight^γ × luminance diffusion (Rec.601)
  - Height-based: Soft threshold transition

## Requirements

- Visual Studio 2022 (MSVC v143)
- CMake 4.x
- Maya 2026 (or 2023 with modified MAYA_LOCATION)
- Redshift 3.x+ (for OSL rendering)
- Windows only

## License

MPL-2.0 (Mozilla Public License 2.0) - See [LICENSE](LICENSE)

## Citation

This implementation is based on:

```
Morten S. Mikkelsen, "Practical Real-Time Hex-Tiling",
Journal of Computer Graphics Techniques, vol. 11, no. 2, pp. 77-94, 2022.
```

Paper: https://jcgt.org/published/0011/03/05/

Reference implementation: https://github.com/mmikk/hextile-demo (MIT License)

## Author

Seitanmen - https://github.com/seitanmen

## Documentation

Additional documentation available in `docs/`:
- [Specification](../../docs/01_HexTile仕様書.md)
- [Implementation Plan](../../docs/03_Maya実装計画.md)
- [Development Notes](../../docs/README.md)
