# verify_v2.py — uvFilterSize/outAlpha 追加と Hypershade 接続の回帰確認
import sys
import maya.standalone
maya.standalone.initialize(name="python")
import maya.cmds as cmds

mll = r"C:/Users/vmtadmin/Downloads/work/hex_tile/maya_hextile/build/Release/VMTHexTilePoc_v2.mll"
ok = True
def check(label, cond):
    global ok; print(("PASS" if cond else "FAIL")+" : "+label); ok = ok and cond

cmds.loadPlugin(mll)
hx = cmds.createNode("VMTHexTilePoc")

# 属性の存在
for a in ("uvCoord", "uvFilterSize", "uvFilterSizeX", "outColor", "outAlpha"):
    check("attr %s exists" % a, cmds.objExists(hx + "." + a))

# Hypershade と同じ自動接続を再現（前回ここで uvFilterSize 接続エラー）
p2d = cmds.shadingNode("place2dTexture", asUtility=True)
cmds.connectAttr(p2d + ".outUV", hx + ".uvCoord", force=True)
cmds.connectAttr(p2d + ".outUvFilterSize", hx + ".uvFilterSize", force=True)
check("outUV -> uvCoord connected", cmds.isConnected(p2d + ".outUV", hx + ".uvCoord"))
check("outUvFilterSize -> uvFilterSize connected",
      cmds.isConnected(p2d + ".outUvFilterSize", hx + ".uvFilterSize"))

# 空 file 接続 + compute
f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
cmds.connectAttr(f + ".outColor", hx + ".colorMaps[0]", force=True)
check("outColor compute", cmds.getAttr(hx + ".outColor") is not None)
check("outAlpha compute", cmds.getAttr(hx + ".outAlpha") is not None)

print("RESULT:", "ALL PASS" if ok else "SOME FAIL")
maya.standalone.uninitialize()
sys.exit(0 if ok else 1)
