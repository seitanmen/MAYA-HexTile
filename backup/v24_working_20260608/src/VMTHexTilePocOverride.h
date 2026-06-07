// VMTHexTilePocOverride.h — VP2 shading node override（動的フラグメント生成）
// Copyright (c) 2026 kawata / VMT.

#pragma once

#include <maya/MPxShadingNodeOverride.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <vector>

class VMTHexTilePocOverride : public MHWRender::MPxShadingNodeOverride
{
public:
    static MHWRender::MPxShadingNodeOverride* creator(const MObject& obj);
    ~VMTHexTilePocOverride() override;

    MHWRender::DrawAPI supportedDrawAPIs() const override;

    // 接続数 N に応じて生成・登録したフラグメント名を返す
    MString fragmentName() const override;

    // 属性 ↔ フラグメントパラメータの対応
    void getCustomMappings(
        MHWRender::MAttributeParameterMappingList& mappings) override;

    // 複数出力: ノード出力プラグ → フラグメント struct メンバ名
    MString outputForConnection(const MPlug& sourcePlug,
                                const MPlug& destinationPlug) override;

    // colorMaps 接続の増減でフラグメントを作り直す
    bool valueChangeRequiresFragmentRebuild(const MPlug* plug) const override;

    void updateDG() override;
    void updateShader(MHWRender::MShaderInstance& shader,
                      const MHWRender::MAttributeParameterMappingList& mappings) override;

private:
    explicit VMTHexTilePocOverride(const MObject& obj);

    void updateMapCount(); // 接続数を算出し、事前登録済みフラグメント名を選ぶ（レンダラ非依存）

    MObject  fObject;
    int      fMapCount = 1;
    MString  fFragmentName;
    float    fLastTileScale = -9999.0f; // 変化時のみログ
    float    fLastRot       = -9999.0f;
    bool     fLoggedParams  = false;
    bool     fLoggedBind    = false;
    bool     fHasHeight     = false;          // height マップ接続有無
    int      fNormalMode    = 0;              // 0=なし,1=OpenGL,2=DirectX
    unsigned fConnMask      = 0;              // colorMaps[i] 接続ビット（固定8スロット）
    unsigned fSrgbMask      = 0;              // colorMaps[i] が sRGB か
    std::vector<MHWRender::MTexture*> fColorTex; // スロット i のテクスチャ（size 8）
    MHWRender::MTexture* fHeightTex = nullptr; // height テクスチャ
    MHWRender::MTexture* fNormalTex = nullptr; // normal テクスチャ
};
