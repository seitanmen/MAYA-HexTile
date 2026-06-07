// VMTHexTilePoc.cpp — VMT HexTile PoC DG node
// Copyright (c) 2026 kawata / VMT.

#include <sstream>
#include <string>

#include "VMTHexTilePoc.h"

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MFloatVector.h>

// 開発用 MTypeId（内部/テスト用レンジ 0x00000000–0x0007ffff）。
// 配布時は Autodesk 割当ブロックの ID に置換すること。
const MTypeId VMTHexTilePoc::id(0x0007F140);
const MString VMTHexTilePoc::typeName("VMTHexTilePoc");
const MString VMTHexTilePoc::classification("texture/2d:drawdb/shader/texture/2d/VMTHexTilePoc");
const MString VMTHexTilePoc::drawClassification("drawdb/shader/texture/2d/VMTHexTilePoc");

MObject VMTHexTilePoc::aColorMap[VMTHexTilePoc::kSlots];
MObject VMTHexTilePoc::aTileScale;
MObject VMTHexTilePoc::aRotStrength;
MObject VMTHexTilePoc::aHeightMap;
MObject VMTHexTilePoc::aHeightWeight;
MObject VMTHexTilePoc::aHeightDelta;
MObject VMTHexTilePoc::aNormalMap;
MObject VMTHexTilePoc::aNormalConvention;
MObject VMTHexTilePoc::aUvCoord;
MObject VMTHexTilePoc::aUCoord;
MObject VMTHexTilePoc::aVCoord;
MObject VMTHexTilePoc::aUvFilterSize;
MObject VMTHexTilePoc::aUvFilterSizeX;
MObject VMTHexTilePoc::aUvFilterSizeY;
MObject VMTHexTilePoc::aOutColor[VMTHexTilePoc::kSlots];
MObject VMTHexTilePoc::aOutColorStd;
MObject VMTHexTilePoc::aOutAlpha;
MObject VMTHexTilePoc::aOutHeight;
MObject VMTHexTilePoc::aOutNormal;

void* VMTHexTilePoc::creator()
{
    return new VMTHexTilePoc();
}

MStatus VMTHexTilePoc::initialize()
{
    MFnNumericAttribute nAttr;
    MFnCompoundAttribute cAttr;
    MStatus st;

    // colorMap0..7 : 固定 8 個の color 入力（file ノードの outColor を接続）。
    // 配列ではなく独立属性にすることで AE の Add New Item を排除し、常に 0..7 を表示。
    for (int i = 0; i < kSlots; ++i) {
        std::ostringstream ln, sn;
        ln << "colorMap" << i;       // ロング名
        sn << "cm" << i;             // ショート名
        aColorMap[i] = nAttr.createColor(ln.str().c_str(), sn.str().c_str(), &st);
        nAttr.setStorable(true);
        nAttr.setConnectable(true);
        nAttr.setReadable(false);
        nAttr.setKeyable(false);
    }

    aTileScale = nAttr.create("tileScale", "tsc", MFnNumericData::kFloat, 2.0, &st);
    nAttr.setMin(0.1); nAttr.setMax(100.0); nAttr.setKeyable(true);

    aRotStrength = nAttr.create("rotStrength", "rot", MFnNumericData::kFloat, 0.5, &st);
    nAttr.setMin(0.0); nAttr.setMax(1.0); nAttr.setKeyable(true);

    // Height/displacement 入力（接続されると高さベースのブレンドに切替）
    aHeightMap = nAttr.createColor("heightMap", "hm", &st);
    nAttr.setStorable(true); nAttr.setConnectable(true);
    nAttr.setReadable(false); nAttr.setKeyable(false);

    aHeightWeight = nAttr.create("heightWeight", "hwt", MFnNumericData::kFloat, 1.0, &st);
    nAttr.setMin(0.0); nAttr.setMax(2.0); nAttr.setKeyable(true);

    aHeightDelta = nAttr.create("heightDelta", "hdl", MFnNumericData::kFloat, 0.2, &st);
    nAttr.setMin(0.01); nAttr.setMax(1.0); nAttr.setKeyable(true);

    // Normal/tangent-space 入力（接続されると hex-tiled normal を outNormal に出力）
    aNormalMap = nAttr.createColor("normalMap", "nm", &st);
    nAttr.setStorable(true); nAttr.setConnectable(true);
    nAttr.setReadable(false); nAttr.setKeyable(false);

    MFnEnumAttribute eAttr;
    aNormalConvention = eAttr.create("normalConvention", "ncv", 0, &st);
    eAttr.addField("OpenGL", 0);   // Y+ up（Maya/OpenGL 標準）
    eAttr.addField("DirectX", 1);  // Y 反転
    eAttr.setStorable(true); eAttr.setKeyable(false);

    // uvCoord（標準 2D テクスチャ: place2dTexture.outUV が接続）
    aUCoord = nAttr.create("uCoord", "u", MFnNumericData::kFloat, 0.0, &st);
    aVCoord = nAttr.create("vCoord", "v", MFnNumericData::kFloat, 0.0, &st);
    aUvCoord = cAttr.create("uvCoord", "uv", &st);
    cAttr.addChild(aUCoord);
    cAttr.addChild(aVCoord);
    cAttr.setStorable(true);
    cAttr.setHidden(true);

    // uvFilterSize（標準 2D テクスチャ: place2dTexture.outUvFilterSize が接続）
    // ※ これが無いと Hypershade の自動接続が失敗し、テクスチャ評価でクラッシュし得る。
    aUvFilterSizeX = nAttr.create("uvFilterSizeX", "fsx", MFnNumericData::kFloat, 0.0, &st);
    aUvFilterSizeY = nAttr.create("uvFilterSizeY", "fsy", MFnNumericData::kFloat, 0.0, &st);
    aUvFilterSize  = cAttr.create("uvFilterSize", "fs", &st);
    cAttr.addChild(aUvFilterSizeX);
    cAttr.addChild(aUvFilterSizeY);
    cAttr.setStorable(true);
    cAttr.setHidden(true);

    // 出力: outColor0..7（固定 8 個。colorMap{i} に対応。未接続スロットは黒）
    for (int i = 0; i < kSlots; ++i) {
        std::ostringstream ln, sn;
        ln << "outColor" << i;
        sn << "ocl" << i;
        aOutColor[i] = nAttr.createColor(ln.str().c_str(), sn.str().c_str(), &st);
        nAttr.setStorable(false);
        nAttr.setWritable(false);
        nAttr.setReadable(true);
    }

    // 標準 "outColor"（texture/2d スワッチ用・slot0 と同値・hidden）
    aOutColorStd = nAttr.createColor("outColor", "ocl", &st);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);
    nAttr.setHidden(true);

    aOutAlpha = nAttr.create("outAlpha", "oa", MFnNumericData::kFloat, 1.0, &st);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);

    aOutHeight = nAttr.create("outHeight", "oh", MFnNumericData::kFloat, 0.0, &st);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);

    aOutNormal = nAttr.createColor("outNormal", "onr", &st);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);
    nAttr.setDefault(0.5f, 0.5f, 1.0f); // OpenGL エンコードの平坦法線

    for (int i = 0; i < kSlots; ++i) addAttribute(aColorMap[i]);
    addAttribute(aTileScale);
    addAttribute(aRotStrength);
    addAttribute(aHeightMap);
    addAttribute(aHeightWeight);
    addAttribute(aHeightDelta);
    addAttribute(aNormalMap);
    addAttribute(aNormalConvention);
    addAttribute(aUvCoord);
    addAttribute(aUvFilterSize);
    for (int i = 0; i < kSlots; ++i) addAttribute(aOutColor[i]);
    addAttribute(aOutColorStd);
    addAttribute(aOutAlpha);
    addAttribute(aOutHeight);
    addAttribute(aOutNormal);

    // 各 outColor{i} は colorMap{i} と tiling 系に依存。
    for (int i = 0; i < kSlots; ++i) {
        attributeAffects(aColorMap[i], aOutColor[i]);
        attributeAffects(aTileScale,   aOutColor[i]);
        attributeAffects(aRotStrength, aOutColor[i]);
        attributeAffects(aUvCoord,     aOutColor[i]);
        attributeAffects(aColorMap[i], aOutColorStd);
    }
    attributeAffects(aUvCoord, aOutColorStd);
    attributeAffects(aUvCoord, aOutAlpha);
    attributeAffects(aHeightMap, aOutHeight);
    attributeAffects(aUvCoord, aOutHeight);
    attributeAffects(aNormalMap, aOutNormal);
    attributeAffects(aNormalConvention, aOutNormal);
    attributeAffects(aTileScale, aOutNormal);
    attributeAffects(aRotStrength, aOutNormal);
    attributeAffects(aUvCoord, aOutNormal);

    return MS::kSuccess;
}

// CPU フォールバック（ソフトレンダ/スワッチ）。
// outColor{i} = colorMap{i} 素通し（未接続は黒）。本物の hex 合成は VP2 側。
MStatus VMTHexTilePoc::compute(const MPlug& plug, MDataBlock& data)
{
    const MObject pAttr = plug.attribute();

    // どの outColor{i}? （要素ではなく独立属性）
    int colorSlot = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (pAttr == aOutColor[i] ||
            (plug.isChild() && plug.parent().attribute() == aOutColor[i])) { colorSlot = i; break; }
    }
    const bool wantStd   = (pAttr == aOutColorStd) ||
        (plug.isChild() && plug.parent().attribute() == aOutColorStd);
    const bool wantAlpha = (plug == aOutAlpha);
    const bool wantHeight = (plug == aOutHeight);
    const bool wantNormal = (pAttr == aOutNormal) ||
        (plug.isChild() && plug.parent().attribute() == aOutNormal);
    if (colorSlot < 0 && !wantStd && !wantAlpha && !wantHeight && !wantNormal)
        return MS::kUnknownParameter;

    MStatus st;
    auto readColor = [&](int i) -> MFloatVector {
        MDataHandle h = data.inputValue(aColorMap[i], &st);
        return st ? h.asFloatVector() : MFloatVector(0.0f, 0.0f, 0.0f);
    };

    if (colorSlot >= 0) {
        MFloatVector c = readColor(colorSlot);
        MDataHandle out = data.outputValue(aOutColor[colorSlot], &st);
        if (st) { out.set3Float(c.x, c.y, c.z); out.setClean(); }
    }
    if (wantStd) {
        MFloatVector c = readColor(0);
        MDataHandle out = data.outputValue(aOutColorStd, &st);
        if (st) { out.set3Float(c.x, c.y, c.z); out.setClean(); }
    }
    if (wantAlpha) {
        MDataHandle outA = data.outputValue(aOutAlpha, &st);
        if (st) { outA.setFloat(1.0f); outA.setClean(); }
    }
    if (wantHeight) {
        MDataHandle outH = data.outputValue(aOutHeight, &st);
        if (st) { outH.setFloat(0.0f); outH.setClean(); }
    }
    if (wantNormal) {
        MDataHandle outN = data.outputValue(aOutNormal, &st);
        if (st) { outN.set3Float(0.5f, 0.5f, 1.0f); outN.setClean(); }
    }
    return MS::kSuccess;
}
