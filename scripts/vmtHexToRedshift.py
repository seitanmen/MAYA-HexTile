# vmtHexToRedshift.py
# VMTHexTilePoc(Maya ノード)から Redshift 用の RedshiftOSLShader ネットワークを
# ワンクリックで自動生成するツール（Redshift には公開翻訳 SDK が無いための運用策）。
#   - 検証済み OSL(vmtHexTile.osl)を読み込み
#   - 接続 file ノードのパス＋colorSpace を抽出して colorMap{i}/srgb{i} に設定
#   - tileScale/rotStrength/tileBlend/height/normal を転送
#   - 生成した outColor0..7 等を Redshift マテリアルに接続すれば描画可能
#
# 使い方（Maya・Redshift ロード済み）:
#   import sys; sys.path.append(r"<repo>/maya_hextile/scripts")   # ← このリポジトリの scripts パス
#   import vmtHexToRedshift as v; reload(v); v.run()        # 選択した VMTHexTilePoc を変換
#
# Copyright (c) 2026 kawata / VMT.

import os
import maya.cmds as cmds
import maya.mel as mel

# OSL ソースはこのスクリプトからの相対位置で解決（PC/ユーザ名に非依存）。
#   scripts/vmtHexToRedshift.py -> ../osl/vmtHexTile.osl
OSL_PATH = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "osl", "vmtHexTile.osl"))


def _set(node, attr, value, as_string=False):
    full = node + "." + attr
    if not cmds.objExists(full):
        return False
    try:
        if as_string:
            cmds.setAttr(full, value, type="string")
        else:
            cmds.setAttr(full, value)
        return True
    except Exception as e:
        print("  warn: setAttr %s -> %r : %s" % (full, value, e))
        return False


def _file_path_srgb(plug):
    """plug の接続元が file ノードならパスと sRGB 要否を返す。"""
    conns = cmds.listConnections(plug, s=True, d=False) or []
    if not conns:
        return ("", True)
    src = conns[0]
    if cmds.nodeType(src) != "file":
        return ("", True)
    path = cmds.getAttr(src + ".fileTextureName") or ""
    cs = (cmds.getAttr(src + ".colorSpace") or "").lower()
    lin = ("linear" in cs) or ("raw" in cs) or ("aces" in cs)
    srgb = ("srgb" in cs) and not lin
    return (path, srgb)


def _transfer(node, osl, attempt=1):
    """OSL コンパイル後に VMTHexTilePoc のパラメータを RedshiftOSLShader へ転送。"""
    if not cmds.objExists(osl):
        return
    # OSL がまだ読み込まれていなければ（colorMap0 が無ければ）少し待って再試行
    if not cmds.objExists(osl + ".colorMap0"):
        if attempt <= 20:
            cmds.evalDeferred(lambda: _transfer(node, osl, attempt + 1), lowestPriority=True)
        else:
            print("[vmtHexToRedshift] 注意: %s の OSL 入力(colorMap0 等)が生成されませんでした。" % osl)
            print("  AE で %s を開いて OSL が読み込まれているか確認してください。" % osl)
            print("  実際の属性一覧:")
            for a in (cmds.listAttr(osl, ud=True) or []):
                print("     " + a)
        return

    # スカラ
    _set(osl, "tileScale",   cmds.getAttr(node + ".tileScale"))
    _set(osl, "rotStrength", cmds.getAttr(node + ".rotStrength"))
    _set(osl, "tileBlend",   cmds.getAttr(node + ".tileBlend"))
    # カラー 8 スロット
    for i in range(8):
        path, srgb = _file_path_srgb(node + ".colorMap%d" % i)
        _set(osl, "colorMap%d" % i, path, as_string=True)
        _set(osl, "srgb%d" % i, 1 if srgb else 0)
    # height
    hp, _hs = _file_path_srgb(node + ".heightMap")
    _set(osl, "heightMap", hp, as_string=True)
    _set(osl, "heightWeight", cmds.getAttr(node + ".heightWeight"))
    _set(osl, "heightDelta",  cmds.getAttr(node + ".heightDelta"))
    # normal
    np_, _ns = _file_path_srgb(node + ".normalMap")
    _set(osl, "normalMap", np_, as_string=True)
    conv = cmds.getAttr(node + ".normalConvention")
    _set(osl, "normalFlipY", 1 if conv == 1 else 0)

    print("[vmtHexToRedshift] %s にパラメータを転送しました。" % osl)
    print("  -> outColor0..7 / outHeight / outNormal を Redshift マテリアルに接続してください。")


def convert(node):
    if not cmds.pluginInfo("redshift4maya", q=True, loaded=True):
        cmds.loadPlugin("redshift4maya")

    osl = cmds.shadingNode("RedshiftOSLShader", asShader=True, name=node + "_RS")

    # ソース = ファイル方式 → OSL を読み込み（動的属性は非同期生成）
    if not _set(osl, "sourceType", 1):     # 1 = ファイル（環境差あれば 0 も試す）
        _set(osl, "sourceType", 0)
    _set(osl, "sourceFilePath", OSL_PATH, as_string=True)
    try:
        mel.eval('rsOSLShader -reloadSourceFile "%s";' % osl)
    except Exception as e:
        print("  warn: reloadSourceFile: %s" % e)
    try:
        cmds.refresh()
    except Exception:
        pass

    print("[vmtHexToRedshift] 生成: %s （OSL コンパイル後にパラメータを自動転送します）" % osl)
    # OSL コンパイル＆属性生成が非同期のため、転送は遅延実行＋リトライ
    cmds.evalDeferred(lambda: _transfer(node, osl), lowestPriority=True)
    return osl


def run():
    sel = cmds.ls(sl=True) or []
    nodes = [n for n in sel if cmds.nodeType(n) == "VMTHexTilePoc"]
    if not nodes:
        cmds.warning("VMTHexTilePoc ノードを選択してから実行してください。")
        return
    made = [convert(n) for n in nodes]
    cmds.select(made, r=True)
    return made
