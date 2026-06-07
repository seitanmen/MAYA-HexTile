// FragmentBuilder.h — VMT HexTile PoC
//
// 接続されたカラーテクスチャ枚数 N に応じて、VP2 シェーディングフラグメント
// (XML + HLSL/GLSL) を実行時に文字列生成する。
//
// 検証対象リスク:
//   ② 動的フラグメント生成（N に応じて texture2/sampler を増やす）
//   ③ 同一テクスチャを 3 つの hex UV で再サンプリングして合成
//
// Copyright (c) 2026 kawata / VMT.

#pragma once

#include <maya/MString.h>

namespace vmt {

// 旧・事前登録方式の上限（dead code 用に残置）。
constexpr int kMaxColorMaps = 8;

// 固定カラースロット数（colorMaps[0..7] ↔ outColor[0..7] を 1:1 固定）。
constexpr int kFixedColorSlots = 8;

// N 枚のカラーテクスチャ入力を持つ shade fragment の XML を生成する。
// fragmentName: 生成されるフラグメント名（シグネチャ一意。例 "VMTHexTilePoc_c2"）
MString buildHexTileFragmentXML(int numColorMaps, const MString& fragmentName);

// シグネチャからフラグメント名を作る（同名は再生成しないキャッシュキー）。
MString hexTileFragmentName(int numColorMaps);

// 1..kMaxColorMaps のフラグメントをフラグメントマネージャへ事前登録/解除する。
// プラグインの initialize/uninitialize から呼ぶ（安全なコンテキスト）。
// renderer 不在（バッチ等）では何もしない。
void registerHexTileFragments();
void deregisterHexTileFragments();

// --- 診断用（v3）: テクスチャを使わず六角タイリングをパターン色で可視化 ---
// 目的: VP2 フラグメント＋六角演算が動作するかを、テクスチャバインドと切り離して確認する。
MString hexVizFragmentName();
MString buildHexVizFragmentXML(const MString& fragmentName);

// --- テクスチャ六角タイリング本体 ---
// decodeSrgb: サンプルを sRGB→linear 変換するか（接続 file の colorSpace で決定）。
// useHeight : height マップで高さベースのブレンド重みを使うか（height 接続有無で決定）。
// normalMode: 0=なし, 1=OpenGL, 2=DirectX(Y反転)
MString texCheckFragmentName(bool decodeSrgb, bool useHeight, int normalMode);   // 公開（グラフ）名
MString texCheckImplName(bool decodeSrgb, bool useHeight, int normalMode);       // leaf(compute) フラグメント名
MString buildTexCheckFragmentXML(const MString& fragmentName, bool decodeSrgb, bool useHeight, int normalMode);

// --- 複数出力（struct + fragment_graph）---
// vmtHexOut{ float3 outColor; float outHeight; float3 outNormal; } を宣言する structure
// フラグメントと、leaf+structure を束ねる fragment_graph。override は graph 名で参照する。
MString vmtHexStructFragmentName();
MString buildVmtHexStructXML();
MString buildTexCheckGraphXML(const MString& graphName, const MString& implName, bool useHeight, int normalMode);

// --- 動的フラグメント生成（真の無制限）---
// シグネチャ = colorMaps 枚数 n ＋ 各マップ sRGB ビットマスク srgbMask
//              ＋ height 有無 ＋ normalMode(0/1/2)。
// registerDynamicFragment: 未登録なら struct/leaf/graph 一式を生成・登録し、グラフ名を返す。
//   renderer 不在では空文字列。override の updateMapCount から呼ぶ。
// 固定 8 スロット方式。connMask=接続スロット(8bit)、srgbMask=sRGBスロット(8bit)。
MString dynFragmentName(unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode);
MString dynStructName();
MString buildDynStructXML();
MString buildDynLeafXML(const MString& implName, unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode);
MString buildDynGraphXML(const MString& graphName, const MString& implName, unsigned connMask, bool useHeight, int normalMode);
MString registerDynamicFragment(unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode);

} // namespace vmt
