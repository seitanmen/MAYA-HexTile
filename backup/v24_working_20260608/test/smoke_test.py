# smoke_test.py — mayapy ヘッドレスでの最小検証
#   - プラグインのロード / ノード登録
#   - colorMaps[] 配列接続
#   - フラグメント XML 生成ロジックの健全性（FragmentBuilder は C++ 側だが、
#     ここではノード登録と接続トポロジまでを確認）
# 実行: & "C:/Program Files/Autodesk/Maya2026/bin/mayapy.exe" smoke_test.py <mll_path>

import sys
import maya.standalone
maya.standalone.initialize(name="python")

import maya.cmds as cmds

mll = sys.argv[1] if len(sys.argv) > 1 else \
    r"C:/Users/vmtadmin/Downloads/work/hex_tile/maya_hextile/build/Release/VMTHexTilePoc.mll"

ok = True
def check(label, cond):
    global ok
    print(("PASS" if cond else "FAIL") + " : " + label)
    ok = ok and cond

try:
    cmds.loadPlugin(mll)
    check("loadPlugin", cmds.pluginInfo("VMTHexTilePoc", q=True, loaded=True))
    check("node registered", "VMTHexTilePoc" in (cmds.pluginInfo("VMTHexTilePoc", q=True, dependNode=True) or []))

    hx = cmds.createNode("VMTHexTilePoc")
    check("createNode", cmds.objExists(hx))

    # 2 枚の file を colorMaps[0],[1] へ
    for i in range(2):
        f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
        cmds.connectAttr(f + ".outColor", "{0}.colorMaps[{1}]".format(hx, i), force=True)
    n = cmds.getAttr(hx + ".colorMaps", size=True)
    check("colorMaps array size == 2", n == 2)

    # スカラ属性
    cmds.setAttr(hx + ".tileScale", 3.0)
    cmds.setAttr(hx + ".rotStrength", 0.6)
    check("tileScale set", abs(cmds.getAttr(hx + ".tileScale") - 3.0) < 1e-5)

    # 出力プラグの存在
    check("outColor plug", cmds.objExists(hx + ".outColor"))

    print("RESULT: " + ("ALL PASS" if ok else "SOME FAIL"))
finally:
    try:
        cmds.unloadPlugin("VMTHexTilePoc")
    except Exception as e:
        print("unload warn:", e)
    maya.standalone.uninitialize()

sys.exit(0 if ok else 1)
