// VMTHexTilePocOverride.cpp — VP2 shading node override
// Copyright (c) 2026 kawata / VMT.
//
// 重要: フラグメントの登録は plugin 初期化時（FragmentBuilder::registerHexTileFragments）
// に行う。override 内ではフラグメントマネージャを変更しない
// （VP2 のシェーダグラフ構築中の再入を避ける。クラッシュ対策）。

// 標準ヘッダは Maya ヘッダより前に
#include <sstream>
#include <string>

#include "VMTHexTilePocOverride.h"
#include "VMTHexTilePoc.h"
#include "FragmentBuilder.h"

#include <maya/MFnDependencyNode.h>
// MAttributeParameterMapping(List) は MPxShadingNodeOverride.h 内で宣言。
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MShaderManager.h>   // MShaderInstance::setParameter
#include <maya/MTextureManager.h>  // テクスチャ自前ロード
#include <maya/MStateManager.h>    // サンプラー状態
#include <maya/MViewport2Renderer.h>
#include <maya/MGlobal.h>          // 診断ログ
#include <maya/MStringArray.h>

using namespace MHWRender;

MPxShadingNodeOverride* VMTHexTilePocOverride::creator(const MObject& obj)
{
    return new VMTHexTilePocOverride(obj);
}

VMTHexTilePocOverride::VMTHexTilePocOverride(const MObject& obj)
    : MPxShadingNodeOverride(obj)
    , fObject(obj)
{
    // 軽量処理のみ（接続数の算出）。レンダラには触れない。
    updateMapCount();
}

VMTHexTilePocOverride::~VMTHexTilePocOverride()
{
    MHWRender::MRenderer* r = MHWRender::MRenderer::theRenderer();
    MHWRender::MTextureManager* tm = r ? r->getTextureManager() : nullptr;
    if (tm) {
        for (MHWRender::MTexture* t : fColorTex) if (t) tm->releaseTexture(t);
        if (fHeightTex) tm->releaseTexture(fHeightTex);
        if (fNormalTex) tm->releaseTexture(fNormalTex);
    }
    fColorTex.clear();
    fHeightTex = nullptr;
    fNormalTex = nullptr;
}

DrawAPI VMTHexTilePocOverride::supportedDrawAPIs() const
{
    return kOpenGL | kDirectX11 | kOpenGLCoreProfile;
}

// 属性に接続された上流ノード（file ノード）を返す。isArray なら要素[0]を見る。
static MObject vmtSourceNode(const MObject& node, const MObject& attr, bool isArray)
{
    MStatus st;
    MFnDependencyNode fn(node, &st);
    if (!st) return MObject::kNullObj;
    MPlug p = fn.findPlug(attr, false, &st);
    if (!st) return MObject::kNullObj;
    if (isArray) {
        if (p.numElements() == 0) return MObject::kNullObj;
        p = p.elementByPhysicalIndex(0, &st);
        if (!st) return MObject::kNullObj;
    }
    MPlugArray srcs;
    p.connectedTo(srcs, true, false);
    return (srcs.length() > 0) ? srcs[0].node() : MObject::kNullObj;
}

// 接続 file の colorSpace 文字列から sRGB デコード要否を判定。
static bool vmtNeedsSrgb(const MObject& fileNode)
{
    if (fileNode.isNull()) return true;
    MStatus st;
    MFnDependencyNode ffn(fileNode);
    MPlug cs = ffn.findPlug("colorSpace", false, &st);
    if (!st) return true;
    MString lo; cs.getValue(lo); lo = lo.toLowerCase();
    bool isLinear = (lo.indexW("linear") >= 0) || (lo.indexW("raw") >= 0) || (lo.indexW("aces") >= 0);
    return (lo.indexW("srgb") >= 0) && !isLinear;
}

void VMTHexTilePocOverride::updateMapCount()
{
    // 固定 8 スロット: colorMap0..7（独立属性）の接続有無と per-map sRGB を収集。
    fConnMask = 0;
    fSrgbMask = 0;
    MStatus st;
    MFnDependencyNode fn(fObject, &st);
    for (int i = 0; i < vmt::kFixedColorSlots; ++i) {
        MObject src = vmtSourceNode(fObject, VMTHexTilePoc::aColorMap[i], false);
        if (src.isNull()) continue;
        fConnMask |= (1u << i);
        if (vmtNeedsSrgb(src)) fSrgbMask |= (1u << i);
    }
    fMapCount = 0;
    for (int i = 0; i < vmt::kFixedColorSlots; ++i) if (fConnMask & (1u << i)) ++fMapCount;

    // heightMap 接続有無。
    fHasHeight = !vmtSourceNode(fObject, VMTHexTilePoc::aHeightMap, false).isNull();

    // normalMap 接続有無 + normalConvention。
    bool hasNormal = !vmtSourceNode(fObject, VMTHexTilePoc::aNormalMap, false).isNull();
    fNormalMode = 0;
    if (hasNormal) {
        int conv = 0; MStatus st3;
        MPlug cp = fn.findPlug(VMTHexTilePoc::aNormalConvention, false, &st3);
        if (st3) cp.getValue(conv);
        fNormalMode = (conv == 1) ? 2 : 1;
    }

    // シグネチャから名前のみ算出（レンダラには触れない＝コンストラクタ安全）。
    // 実際の生成・登録は fragmentName()（VP2 のシェーダ構築コールバック）で行う。
    fFragmentName = vmt::dynFragmentName(fConnMask, fSrgbMask, fHasHeight, fNormalMode);
}

MString VMTHexTilePocOverride::fragmentName() const
{
    // ここはシェーダ構築時に Maya から呼ばれる安全なコンテキスト。
    // 当該シグネチャのフラグメント一式を（未登録なら）生成・登録する（冪等）。
    MString gname = vmt::registerDynamicFragment(fConnMask, fSrgbMask, fHasHeight, fNormalMode);
    return (gname.length() > 0) ? gname : fFragmentName;
}

MString VMTHexTilePocOverride::outputForConnection(const MPlug& sourcePlug,
                                                   const MPlug& /*destinationPlug*/)
{
    MPlug p = sourcePlug;
    if (p.isChild()) p = p.parent();          // R/G/B → 親へ
    const MObject attr = p.attribute();
    if (attr == VMTHexTilePoc::aOutHeight) return MString("outHeight");
    if (attr == VMTHexTilePoc::aOutNormal) return MString("outNormal");
    if (attr == VMTHexTilePoc::aOutColorStd) return MString("outColor0"); // 標準=slot0

    // 固定 1:1: outColor{i} → struct メンバ outColor{i}（i = 0..7）。
    for (int i = 0; i < vmt::kFixedColorSlots; ++i)
        if (attr == VMTHexTilePoc::aOutColor[i]) {
            std::ostringstream o; o << "outColor" << i; return MString(o.str().c_str());
        }
    return MString("outColorNull");
}

void VMTHexTilePocOverride::updateDG()
{
    updateMapCount();
}

void VMTHexTilePocOverride::getCustomMappings(
    MAttributeParameterMappingList& mappings)
{
    // スカラ: uvScale←tileScale, rotAmt←rotStrength（rebuild で反映）
    mappings.append(MAttributeParameterMapping("uvScale", "tileScale",   true, true));
    mappings.append(MAttributeParameterMapping("rotAmt",  "rotStrength", true, true));
    // 各カラースロット: map{i} ← colorMap{i}（接続スロットのみ。実体は updateShader でバインド）
    for (int i = 0; i < vmt::kFixedColorSlots; ++i) if (fConnMask & (1u << i)) {
        std::ostringstream pn; pn << "map" << i;
        std::ostringstream an; an << "colorMap" << i;
        mappings.append(MAttributeParameterMapping(pn.str().c_str(), an.str().c_str(), true, false));
    }
    if (fHasHeight) {
        mappings.append(MAttributeParameterMapping("heightWeight", "heightWeight", true, true));
        mappings.append(MAttributeParameterMapping("heightDelta",  "heightDelta",  true, true));
        mappings.append(MAttributeParameterMapping("mapH", "heightMap", true, false));
    }
    if (fNormalMode != 0) {
        mappings.append(MAttributeParameterMapping("mapNrm", "normalMap", true, false));
    }
}

bool VMTHexTilePocOverride::valueChangeRequiresFragmentRebuild(const MPlug* plug) const
{
    if (plug) {
        const MObject attr = plug->attribute();
        // colorMap{i}: 接続変化 → 別シグネチャのフラグメントへ切替。
        // tileScale/rotStrength: 定数として焼き込まれるため値変更時は再ビルドが必要。
        for (int i = 0; i < vmt::kFixedColorSlots; ++i)
            if (attr == VMTHexTilePoc::aColorMap[i]) return true;
        if (attr == VMTHexTilePoc::aTileScale ||
            attr == VMTHexTilePoc::aRotStrength ||
            attr == VMTHexTilePoc::aHeightMap ||
            attr == VMTHexTilePoc::aHeightWeight ||
            attr == VMTHexTilePoc::aHeightDelta ||
            attr == VMTHexTilePoc::aNormalMap ||
            attr == VMTHexTilePoc::aNormalConvention)
            return true;
    }
    return MPxShadingNodeOverride::valueChangeRequiresFragmentRebuild(plug);
}

void VMTHexTilePocOverride::updateShader(
    MShaderInstance& shader,
    const MAttributeParameterMappingList& mappings)
{
    using namespace MHWRender;
    MStatus st;
    MFnDependencyNode fn(fObject, &st);
    if (!st) return;

    // 初回のみ、シェーダ公開パラメータ一覧をログ（テクスチャ param 名特定用）
    if (!fLoggedParams) {
        fLoggedParams = true;
        MStringArray params;
        shader.parameterList(params);
        MString line = "[VMTHex] shader params:";
        for (unsigned i = 0; i < params.length(); ++i) line += MString(" ") + params[i];
        MGlobal::displayInfo(line);
    }

    MRenderer* renderer = MRenderer::theRenderer();
    MTextureManager* texMgr = renderer ? renderer->getTextureManager() : nullptr;
    if (!texMgr) return;

    // 共通サンプラー状態（線形・wrap）
    MSamplerStateDesc desc;
    desc.filter   = MSamplerState::kMinMagMipLinear;
    desc.addressU = MSamplerState::kTexWrap;
    desc.addressV = MSamplerState::kTexWrap;
    const MSamplerState* ss = MStateManager::acquireSamplerState(desc);

    // file ノードのテクスチャを自前ロードして texParam/sampParam にバインド
    auto bindTex = [&](const MObject& fileNode, MTexture*& slot,
                       const char* mapParam, const char* texP, const char* sampP) -> bool
    {
        if (slot) { texMgr->releaseTexture(slot); slot = nullptr; }
        if (fileNode.isNull()) return false;
        slot = texMgr->acquireTexture(fileNode, false);
        if (!slot) return false;
        const MAttributeParameterMapping* m = mappings.findByParameterName(mapParam);
        MString name = m ? m->resolvedParameterName() : MString(texP);
        MTextureAssignment ta; ta.texture = slot;
        bool ok = (bool)shader.setParameter(name, ta);
        if (ss) ok = ok && (bool)shader.setParameter(sampP, *ss);
        return ok;
    };

    // 接続スロット i のテクスチャを map{i} にバインド（固定8スロット, index=論理index）。
    for (MHWRender::MTexture* t : fColorTex) if (t) texMgr->releaseTexture(t);
    fColorTex.assign((size_t)vmt::kFixedColorSlots, nullptr);
    bool okC = true;
    for (int i = 0; i < vmt::kFixedColorSlots; ++i) if (fConnMask & (1u << i)) {
        std::ostringstream pn; pn << "map" << i;
        std::ostringstream sp; sp << "map" << i << "Sampler";
        std::string p = pn.str(), s = sp.str();
        bool ok = bindTex(vmtSourceNode(fObject, VMTHexTilePoc::aColorMap[i], false),
                          fColorTex[i], p.c_str(), p.c_str(), s.c_str());
        okC = okC && ok;
    }
    bool okH = true;
    if (fHasHeight)
        okH = bindTex(vmtSourceNode(fObject, VMTHexTilePoc::aHeightMap, false),
                      fHeightTex, "mapH", "mapH", "mapHSampler");
    bool okN = true;
    if (fNormalMode != 0)
        okN = bindTex(vmtSourceNode(fObject, VMTHexTilePoc::aNormalMap, false),
                      fNormalTex, "mapNrm", "mapNrm", "mapNrmSampler");

    if (!fLoggedBind || !okC || (fHasHeight && !okH) || (fNormalMode != 0 && !okN)) {
        fLoggedBind = true;
        MGlobal::displayInfo(MString("[VMTHex] bind colors=") + fMapCount + (okC ? " OK" : " FAIL")
            + " height=" + (fHasHeight ? (okH ? "OK" : "FAIL") : MString("-"))
            + " normal=" + (fNormalMode != 0 ? (okN ? "OK" : "FAIL") : MString("-")));
    }
}
