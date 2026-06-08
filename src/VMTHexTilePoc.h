// VMTHexTilePoc.h — VMT HexTile PoC DG node
//
// 固定 8 スロット: colorMap0..colorMap7（入力）↔ outColor0..outColor7（出力）を
// 1:1 で持つ（配列廃止＝AE の Add New Item を構造的に排除）。
// VP2 表示は VMTHexTilePocOverride（MPxShadingNodeOverride）が担当する。
//
// Copyright (c) 2026 kawata / VMT.

#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>
#include <maya/MObject.h>

class VMTHexTilePoc : public MPxNode
{
public:
    VMTHexTilePoc() = default;
    ~VMTHexTilePoc() override = default;

    static void*    creator();
    static MStatus  initialize();
    MStatus         compute(const MPlug& plug, MDataBlock& data) override;

    static const int kSlots = 8; // 固定スロット数

    static const MTypeId id;
    static const MString  typeName;
    static const MString  classification;   // registerNode 用
    static const MString  drawClassification; // drawdb（override 用）

    // attributes
    static MObject aColorMap[kSlots];  // colorMap0..7 : color 入力（file.outColor を接続）
    static MObject aTileScale;   // float
    static MObject aRotStrength; // float
    static MObject aTileBlend;   // float（タイル境界のボケ/シャープ。0.5=無調整, <0.5=ボケ, >0.5=シャープ）
    static MObject aHeightMap;   // color 入力（height/displacement file を接続）
    static MObject aHeightWeight;// float
    static MObject aHeightDelta; // float
    static MObject aNormalMap;   // color 入力（tangent-space normal file を接続, Raw）
    static MObject aNormalConvention; // enum 0=OpenGL,1=DirectX(Y反転)
    static MObject aUvCoord;     // compound(uCoord,vCoord) — 標準2Dテクスチャ
    static MObject aUCoord;
    static MObject aVCoord;
    static MObject aUvFilterSize;  // compound(uvFilterSizeX,Y) — place2dTexture が接続
    static MObject aUvFilterSizeX;
    static MObject aUvFilterSizeY;
    static MObject aOutColor[kSlots]; // outColor0..7 : color 出力（hex-tiled、未接続は黒）
    static MObject aOutColorStd; // "outColor" 標準出力（texture/2d 用・slot0・hidden）
    static MObject aOutAlpha;    // float 出力（標準2Dテクスチャ）
    static MObject aOutHeight;   // float 出力（hex-tiled ハイト）
    static MObject aOutNormal;   // 3float 出力（hex-tiled ノーマル, OpenGLエンコード[0,1]）
};
