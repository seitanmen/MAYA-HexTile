# compute_repro.py — compute() を強制実行してクラッシュを再現する。
import sys
import os
import maya.standalone
maya.standalone.initialize(name="python")
import maya.cmds as cmds

mll = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "build", "Release", "VMTHexTilePoc.mll"))

cmds.loadPlugin(mll)
hx = cmds.createNode("VMTHexTilePoc")
print("created:", hx)

# 空の file を colorMaps[0] へ（AE の Add New Item 相当）
f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
cmds.connectAttr(f + ".outColor", hx + ".colorMaps[0]", force=True)
print("connected empty file -> colorMaps[0]")

# compute を強制: outColor を評価
sys.stdout.flush()
print(">>> about to getAttr outColor (forces compute)")
sys.stdout.flush()
val = cmds.getAttr(hx + ".outColor")
print(">>> getAttr outColor OK:", val)

# uvCoord を設定して再評価
cmds.setAttr(hx + ".tileScale", 4.0)
val2 = cmds.getAttr(hx + ".outColorR")
print(">>> getAttr outColorR OK:", val2)

print("RESULT: NO_CRASH")
maya.standalone.uninitialize()
