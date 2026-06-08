# vmtHexToRedshift.py
# VMTHexTilePoc(Maya ノード)から Redshift 用の RedshiftOSLShader ネットワークを
# ワンクリックで自動生成するツール（Redshift には公開翻訳 SDK が無いための運用策）。
#   - 検証済み OSL(vmtHexTile.osl)を読み込み
#   - 接続 file ノードのパス＋colorSpace を抽出して colorMap{i}/srgb{i} に設定
#   - tileScale/rotStrength/tileBlend/height/normal を転送
#   - 生成した outColor0..7 等を Redshift マテリアルに接続すれば描画可能
#
# 使い方（Maya・Redshift ロード済み）:
#   import sys; sys.path.append(r"C:/Users/vmtadmin/Downloads/work/Maya_hex_tile/maya_hextile/scripts")
#   import vmtHexToRedshift as v; reload(v); v.run()        # 選択した VMTHexTilePoc を変換
#
# Copyright (c) 2026 kawata / VMT.

import maya.cmds as cmds
import maya.mel as mel

OSL_PATH = r"C:/Users/vmtadmin/Downloads/work/Maya_hex_tile/maya_hextile/osl/vmtHexTile.osl"


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


def convert(node):
    if not cmds.pluginInfo("redshift4maya", q=True, loaded=True):
        cmds.loadPlugin("redshift4maya")

    osl = cmds.shadingNode("RedshiftOSLShader", asShader=True, name=node + "_RS")

    # ソース = ファイル方式（sourceType の値は環境差があるので両方試す）
    if not _set(osl, "sourceType", 1):
        _set(osl, "sourceType", 0)
    _set(osl, "sourceFilePath", OSL_PATH, as_string=True)
    # コンパイル＆動的属性の再生成
    try:
        mel.eval('rsOSLShader -reloadSourceFile "%s";' % osl)
    except Exception as e:
        print("  warn: reloadSourceFile: %s" % e)

    # 実際に作られた動的属性を表示（命名確認用）
    print("\n==== %s のダイナミック属性 ====" % osl)
    for a in (cmds.listAttr(osl, ud=True) or []):
        print("   " + a)
    print("==== ここまで ====\n")

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

    print("[vmtHexToRedshift] 生成: %s" % osl)
    print("  -> outColor0..7 / outHeight / outNormal を Redshift マテリアルに接続してください。")
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
