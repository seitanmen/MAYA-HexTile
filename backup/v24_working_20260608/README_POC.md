# VMT HexTile — Phase 1 中核 PoC

> 目的: 単一ノード設計の**最大リスク 3 点**を Maya 2026（VP2/DX11）で実証する。
> 関連: [../docs/03_Maya実装計画.md](../docs/03_Maya実装計画.md) Phase 1

## この PoC が検証すること

| # | リスク | この PoC での検証方法 | 状態 |
|---|---|---|---|
| ③ | **同一テクスチャを 3 つの hex UV で再サンプリング** | フラグメントが `texture2`+`sampler` を受け、`SampleGrad` で 3 回サンプリング | コード実装済・**実機確認要** |
| ② | **動的フラグメント生成＋再ビルド** | 接続数 N から XML/HLSL を実行時生成し `addShadeFragmentFromBuffer`、`colorMaps[1]` 追加で再生成 | コード実装済・**実機確認要** |
| ① | **複数/配列出力** | 本 PoC は**単一出力**。①は下記「次の実験」で別途検証 | 未着手（手順のみ） |

> ✅ **コンパイル検証済み**: Maya 2026 SDK / VS2022(v143) / CMake 4.x で `VMTHexTilePoc.mll` ビルド成功（2026-06-07）。
> ✅ **ヘッドレス検証済み**: `mayapy` + `test/smoke_test.py` で **ALL PASS**
>    （loadPlugin / ノード登録 / createNode / **colorMaps[] に 2 枚接続→配列サイズ2** / 属性 set / outColor プラグ）。
> ⚠️ **VP2 描画（ビューポート表示）は未検証**（GUI 起動が必要）。下記チェックリストを実機 GUI で実施すること。
>    ヘッドレスでは `MRenderer`/フラグメント生成は走らないため、リスク①②③の**視覚確認は GUI 必須**。
>
> ビルド時に判明した実装上の注意（コードに反映済み）:
> - 標準ヘッダ(`<sstream>` 等)は **Maya ヘッダより前**に include（Maya マクロが標準ライブラリ解析を壊す既知問題）。
> - MSVC は `/utf-8` 必須（UTF-8 ソースの cp932 誤読 C4819 回避）。
> - `MAttributeParameterMapping(List)` は専用ヘッダが無く `MPxShadingNodeOverride.h` 内で宣言。

## 構成

```
maya_hextile/
├── CMakeLists.txt
├── cmake/MayaConfig.cmake          # Maya install から include/lib を解決
├── src/
│   ├── pluginMain.cpp              # registerNode + registerShadingNodeOverrideCreator
│   ├── VMTHexTilePoc.{h,cpp}       # DG ノード（colorMaps[] 配列入力）
│   ├── VMTHexTilePocOverride.{h,cpp} # MPxShadingNodeOverride（動的フラグメント）
│   └── FragmentBuilder.{h,cpp}     # N枚→XML+HLSL/GLSL 文字列生成（hex 再サンプリング）
└── test/setup_poc.mel             # テストシーン構築
```

## ビルド（Windows / Maya 2026）

VS2022 + CMake。Maya 2026 のヘッダ/ライブラリは既存インストールを使用。

```powershell
cd C:/Users/vmtadmin/Downloads/work/hex_tile/maya_hextile
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2026"
cmake --build build --config Release
# => build/Release/VMTHexTilePoc.mll
```

> 注: Maya 2026 は VS2022/v143 ツールセット想定。Maya 2023 でビルドするなら
> `-DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2023"`（こちらは VS2019/v142 推奨）。

## ロード & テスト

```mel
// Script Editor (MEL)
loadPlugin "C:/Users/vmtadmin/Downloads/work/hex_tile/maya_hextile/build/Release/VMTHexTilePoc.mll";
source "C:/Users/vmtadmin/Downloads/work/hex_tile/maya_hextile/test/setup_poc.mel";
vmtPocBuild("C:/path/to/tileable_texture.png");   // タイラブルな画像を指定
```

### 検証チェックリスト（実機）
- [ ] **③ 再サンプリング**: 平面が単純な UV 貼りではなく、**六角状にランダム回転・オフセットされたタイル**で表示される。`tileScale`/`rotStrength` を変えて即時反映するか。
  - 失敗の兆候: テクスチャが普通にタイル表示されるだけ → `texture2` バインドが効かず file が現在 UV で解決されている。→ §トラブルシュート。
- [ ] **② 動的生成**: `vmtPocAddMap("...");` で `colorMaps[1]` を接続 → フラグメントが `VMTHexTilePoc_c1`→`_c2` に再生成され、表示が 2 枚平均に変わる。Script Editor にエラーが出ないか。
- [ ] フラグメント登録の成否: 下記でダンプして XML/HLSL が想定通りか確認。

## フラグメントのデバッグ・ダンプ

Maya は生成エフェクトをディスクへ出力できる（実機で実行）:

```mel
// 生成された最終エフェクト(HLSL/GLSL)を指定フォルダへダンプ
// MFragmentManager::setEffectOutputDirectory 相当の MEL/Python:
python("import maya.api.OpenMayaRender as omr; "
       "r=omr.MRenderer.theRenderer(); "
       "fm=r.getFragmentManager(); "
       "fm.setEffectOutputDirectory('C:/temp/vmt_fx'); "
       "print(fm.fragmentList())");
// 既存ノード(例: file/checker)のフラグメントと比較し、texture2/sampler の
// 正確なバインド記法（semantic 名など）を確認する。
```

## トラブルシュート（実機で詰まりやすい点）

1. **テクスチャがタイル化されず普通に出る（③失敗）**
   - 原因候補: `texture2` param への color 属性マッピングで、Maya が file を「テクスチャ」ではなく「ベイク済み色」としてバインドしている。
   - 対処: (a) 既存の file/checker ノードのフラグメントをダンプし、texture2 入力に付く `semantic`（例 `mayaTextureSemantic` 等）を確認して `<texture2>`/`<sampler>` 宣言に追記。(b) サンプラー param 名の規約（`<tex>Sampler`）を実機ログに合わせる。
2. **フラグメント未登録（`addShadeFragmentFromBuffer` が空文字を返す）**
   - 原因: XML スキーマ不正（要素名/属性名）。`MGlobal::displayError` を override に追加してログ確認。
   - 対処: `class="ShadeFragment"`, `type="plumbing"`, `<implementation render="OGSRenderer" ...>` の綴り、function シグネチャとプロパティ順の一致を確認。
3. **複数出力（①）**
   - 本 PoC は単一 `outColor`。次の実験で `<outputs>` に `outColor`+`outColor2` を追加し、両方を別 lambert に繋いで VP2 がノードを一度だけ評価して両出力を出せるか確認する。
   - 不可なら計画通り「出力系統ごとのノード分割」または「capped 固定スロット」に切替（[../docs/03_Maya実装計画.md](../docs/03_Maya実装計画.md) Phase 1 失敗時代替）。

## クラッシュ診断と修正（2026-06-07）

**症状**: AE の「Add New Item」で colorMaps に空の file ノードを接続した直後に Maya がクラッシュ。

**診断**（CER ダンプ `MayaCrashLog260607.0151.dmp` を解析。`test/parse_dmp.py` / `walk_dmp.py`）:
- 例外: `0xC0000005`（アクセス違反）@ `Foundation.dll`。
- スタックに `RenderModel.dll`（スワッチ/VP2 シェーダ）+ `DependEngine.dll` + `Foundation.dll` + 本プラグイン。
- 切り分け（ヘッドレス `mayapy`）: DG `compute()` は正常 / フラグメント XML 登録も成功（`frag_probe.py` → `OK_REGISTERED`）。
  → クラッシュは **GUI/VP2 のオーバーライド経路のみ**で発生（ヘッドレスでは到達しない）。

**原因（最有力）**: 旧実装は **`MPxShadingNodeOverride` のコンストラクタ内で `addShadeFragmentFromBuffer`** を呼んでいた。
VP2 がシェーダグラフを構築している最中にフラグメントマネージャを変更する＝再入で内部状態を壊し、後続の Foundation 呼び出しで AV。
Autodesk ガイダンスも「フラグメント登録はプラグインロード時」。

**修正**: フラグメント登録を **`initializePlugin` 時に 1..8 枚分を事前登録**（`FragmentBuilder::registerHexTileFragments`）に移動。
override はレンダラに一切触れず、接続数から事前登録済みフラグメント名を選ぶだけにした。
→ 当面 **最大 8 枚**（真の無制限＝動的生成は安定確認後に再導入）。

**検証**: `test/verify_fix.py`（ヘッドレス）で ロード / c1・c2・c8 事前登録 / 3枚接続 / compute すべて PASS。
**GUI での再発有無は要実機確認**（クラッシュ経路が GUI 限定のため）。

## クラッシュ診断・修正 その2（2026-06-07, v2）

**追加症状**: Hypershade で生成すると `place2dTexture.outUvFilterSize → VMTHexTilePoc.uvFilterSize` の自動接続が
「destination attribute ... cannot be found」で失敗。その後 file 接続で **同一署名のクラッシュが再発**（v1 修正は無効だった）。

**再診断**: 新ダンプも `0xC0000005 @ Foundation.dll+0x86CB3`、`RenderModel`(2Dテクスチャ評価/スワッチ)→`DependEngine`→Foundation の同一経路。
→ v1 の「コンストラクタ内フラグメント登録」仮説は**棄却**。

**真因（最有力）**: ノードを `texture/2d` に分類していたが、**標準 2D テクスチャ属性 `uvFilterSize`（と `outAlpha`）が欠落**。
Maya の 2D テクスチャ評価フレームワーク（RenderModel）が存在前提の `uvFilterSize` プラグを参照 → 無効プラグの逆参照で Foundation が AV。
Hypershade の接続エラーと同根。

**修正（v2）**: `uvFilterSize`(uvFilterSizeX/Y) と `outAlpha` を追加。compute も両出力対応＋防御的ガード。
**検証**: `test/verify_v2.py`（ヘッドレス）で 属性存在 / **outUvFilterSize→uvFilterSize 接続成功** / compute すべて PASS。
GUI スワッチのクラッシュ再発有無は**要実機確認**（GUI 限定経路のため）。

> ビルド名: Maya が旧 `.mll` をロード中だとリンクがロックされるため、本バージョンは **`VMTHexTilePoc_v2.mll`** として出力。
> `-DPLUGIN_OUTPUT_NAME=...` で出力名を変更可能。

## 黒画面の切り分け（2026-06-07, v3 診断ビルド）

**症状**: クラッシュ解消後、平面に当てると VP2 でも Arnold でも黒。

**整理**:
- Arnold が黒: 現状 **OSL 未実装** のため想定内（Arnold は VP2 フラグメントを使わない。Phase 5）。今は VP2 が対象。
- VP2 が黒: ①file が空（画像未設定）/ ②**テクスチャ実体バインド（リスク③）が効かずフラグメントが黒を返す** のどちらか。

**v3 診断ビルド**: テクスチャを一切使わず、六角タイリングを**手続き的なパターン色**で可視化するフラグメント
（`VMTHexTilePoc_viz`：タイルごとに `hash3` で色を割り当て W でブレンド）に切替。override はこれを使う。
→ **ビューポートに色付きの六角セルが見えれば、VP2 フラグメント＋六角演算は完全動作**＝残課題はテクスチャバインドのみと確定。
→ それでも黒なら、override/フラグメントが適用されていない（より深い問題）。

> テクスチャ版へ戻す: `VMTHexTilePocOverride::updateMapCount()` の `fFragmentName` を `hexVizFragmentName()` →
> `hexTileFragmentName(n)` に、`getCustomMappings()` の texture マッピング（コメントアウト中）を復活。

**設計メモ（リスク③の本命解）**: VP2 フラグメントグラフでは上流 file ノードは「解決済み色」を渡すため、
`texture2` param に繋いでも生サンプラーが来ない可能性が高い。任意 UV 再サンプリングの本筋は
**ノード自身がファイルパス属性を持ち、override が `MTextureManager` でテクスチャをロードして
`updateShader` で `setParameter`（texture 割当）する**方式。v3 の結果次第でこちらへ移行する。

## 既知の制限（PoC スコープ）

- 出力は N 枚の hex 結果の**平均**（視覚確認用）。本実装は規格書 §4/§5 の共有 W・per-map 出力に置換する。
- Height ブレンド・ノーマル処理・OSL は本 PoC 範囲外（Phase 3–5）。
- `MTypeId 0x0007F140` は内部テスト用。配布時に要差し替え。
- GLSL 実装は best-effort（Windows 既定 DX11 のため HLSL が主）。
