// VMTHexTileArnold.cpp — MtoA 翻訳プラグイン（Arnold 拡張）
//   VMTHexTilePoc(Maya ノード) → OSL ノード "vmtHexTile" に翻訳し、
//   ノードを置き換えることなく Arnold で六角タイリングを描画する。
//
//   配置:
//     - vmtHexTile.oso を ARNOLD_PLUGIN_PATH 上に置く（Arnold が node 型として読む）
//     - 本 .dll を MTOA_EXTENSIONS_PATH 上に置く（MtoA が翻訳器を登録）
//
// Copyright (c) 2026 kawata / VMT.

// 標準ヘッダは Maya/MtoA より前に
#include <string>

#include "extension/Extension.h"
#include "translators/shader/ShaderTranslator.h"

#include <ai_nodes.h>

#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MString.h>
#include <maya/MStatus.h>

// 接続元 file ノードからテクスチャパスと sRGB 要否を取得（未接続/非file は ""）
static MString vmtColorPath(const MPlug& plug, bool& isSrgb)
{
    isSrgb = true;
    MPlugArray srcs;
    plug.connectedTo(srcs, true, false);
    if (srcs.length() == 0) return MString("");
    MStatus st;
    MFnDependencyNode fn(srcs[0].node(), &st);
    if (!st) return MString("");
    MPlug ftn = fn.findPlug("fileTextureName", false, &st);
    if (!st) return MString("");   // file 系以外はスキップ（PoC は file 接続前提）
    MString path = ftn.asString();
    MPlug cs = fn.findPlug("colorSpace", false, &st);
    if (st) {
        MString lo = cs.asString().toLowerCase();
        bool lin = (lo.indexW("linear") >= 0) || (lo.indexW("raw") >= 0) || (lo.indexW("aces") >= 0);
        isSrgb = (lo.indexW("srgb") >= 0) && !lin;
    }
    return path;
}

class CVMTHexTileTranslator : public CShaderTranslator
{
public:
    static void* creator() { return new CVMTHexTileTranslator(); }

    // OSL シェーダ "vmtHexTile" を Arnold ノードとして生成
    AtNode* CreateArnoldNodes() override
    {
        return AddArnoldNode("vmtHexTile");
    }

    void Export(AtNode* node) override
    {
        if (!node) return;

        AiNodeSetFlt(node, "tileScale",   FindMayaPlug("tileScale").asFloat());
        AiNodeSetFlt(node, "rotStrength", FindMayaPlug("rotStrength").asFloat());

        // colorMap0..7（接続 file のパス＋per-map sRGB）
        for (int i = 0; i < 8; ++i) {
            MString cmName = MString("colorMap") + i;
            MString sgName = MString("srgb") + i;
            bool srgb = true;
            MString path = vmtColorPath(FindMayaPlug(cmName), srgb);
            AiNodeSetStr(node, cmName.asChar(), path.asChar());
            AiNodeSetInt(node, sgName.asChar(), srgb ? 1 : 0);
        }

        // height
        bool dummy = false;
        MString hp = vmtColorPath(FindMayaPlug("heightMap"), dummy);
        AiNodeSetStr(node, "heightMap", hp.asChar());
        AiNodeSetFlt(node, "heightWeight", FindMayaPlug("heightWeight").asFloat());
        AiNodeSetFlt(node, "heightDelta",  FindMayaPlug("heightDelta").asFloat());

        // normal（convention enum: 0=OpenGL,1=DirectX → normalFlipY）
        MString np = vmtColorPath(FindMayaPlug("normalMap"), dummy);
        AiNodeSetStr(node, "normalMap", np.asChar());
        int conv = FindMayaPlug("normalConvention").asInt();
        AiNodeSetInt(node, "normalFlipY", (conv == 1) ? 1 : 0);
    }
};

// ---------------------------------------------------------------------------
// MtoA 拡張エントリポイント
// ---------------------------------------------------------------------------
extern "C"
{
EXPORT_API_VERSION

DLLEXPORT void initializeExtension(CExtension& extension)
{
    extension.Requires("mtoa");
    // VMTHexTilePoc を OSL ノード vmtHexTile に翻訳する
    extension.RegisterTranslator("VMTHexTilePoc", "",
                                 CVMTHexTileTranslator::creator, NULL);
}

DLLEXPORT void deinitializeExtension(CExtension& /*extension*/)
{
}
}
