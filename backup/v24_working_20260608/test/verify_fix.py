# verify_fix.py — 修正版の回帰確認（ヘッドレス）
import sys
import maya.standalone
maya.standalone.initialize(name="python")
import maya.cmds as cmds
import maya.api.OpenMayaRender as omr

mll = r"C:/Users/vmtadmin/Downloads/work/Maya_hex_tile/maya_hextile/build/Release/VMTHexTilePoc.mll"
ok = True
def check(label, cond):
    global ok; print(("PASS" if cond else "FAIL")+" : "+label); ok = ok and cond

cmds.loadPlugin(mll)
check("loaded", cmds.pluginInfo("VMTHexTilePoc", q=True, loaded=True))

# init で事前登録された 1..8 のフラグメントを確認
fm = omr.MRenderer.getFragmentManager()
if fm:
    for n in (1, 2, 8):
        check("fragment VMTHexTilePoc_c%d registered" % n,
              fm.hasFragment("VMTHexTilePoc_c%d" % n))
else:
    print("NOTE: fragmentManager None (skip fragment check)")

# ノード作成 + 空 file 接続 + compute 強制
hx = cmds.createNode("VMTHexTilePoc")
for i in range(3):
    f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
    cmds.connectAttr(f + ".outColor", "%s.colorMaps[%d]" % (hx, i), force=True)
check("colorMaps size 3", cmds.getAttr(hx + ".colorMaps", size=True) == 3)
check("compute outColor", cmds.getAttr(hx + ".outColor") is not None)

print("RESULT:", "ALL PASS" if ok else "SOME FAIL")
maya.standalone.uninitialize()
sys.exit(0 if ok else 1)
