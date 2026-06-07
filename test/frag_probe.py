# frag_probe.py — FragmentBuilder が出力するのと同じ XML を addShadeFragmentFromBuffer に通し、
# 登録が成功するか（= XML スキーマが妥当か）をヘッドレスで検証する。
import maya.standalone
maya.standalone.initialize(name="python")
import maya.api.OpenMayaRender as omr

# C++ FragmentBuilder::buildHexTileFragmentXML(1, "VMTHexTilePoc_c1") と同じ構造を再現
N = 1
fn = "VMTHexTilePoc_c1"

props = ['    <float2 name="uvCoord" semantic="mayaUvCoordSemantic" flags="varyingInputParam"/>',
         '    <float name="tileScale"/>',
         '    <float name="rotStrength"/>']
for i in range(N):
    props.append('    <texture2 name="map%d"/>' % i)
    props.append('    <sampler name="map%dSampler"/>' % i)

hlsl = "float3 %s(float2 uvCoord, float tileScale, float rotStrength" % fn
for i in range(N):
    hlsl += ", Texture2D map%d, sampler map%dSampler" % (i, i)
hlsl += ") { return float3(uvCoord, 0.0f); }"

xml = (
'<fragment uiName="%s" name="%s" type="plumbing" class="ShadeFragment" version="1.0">\n' % (fn, fn) +
'  <description><![CDATA[probe]]></description>\n'
'  <properties>\n' + "\n".join(props) + "\n  </properties>\n"
'  <values>\n'
'    <float name="tileScale" value="2.0"/>\n'
'    <float name="rotStrength" value="0.5"/>\n'
'  </values>\n'
'  <outputs>\n'
'    <float3 name="outColor"/>\n'
'  </outputs>\n'
'  <implementation>\n'
'    <implementation render="OGSRenderer" language="HLSL" lang_version="11.0">\n'
'      <function_name val="%s"/>\n' % fn +
'      <source><![CDATA[\n' + hlsl + '\n]]></source>\n'
'    </implementation>\n'
'  </implementation>\n'
'</fragment>\n'
)

try:
    fm = omr.MRenderer.getFragmentManager()
except Exception as e:
    fm = None
    print("getFragmentManager raised:", e)
print("fragmentManager:", fm)
if fm is None:
    print("RESULT: FRAGMENT_MANAGER_NONE (VP2 unavailable headless)")
else:
    name = fm.addShadeFragmentFromBuffer(xml.encode("utf-8"), False)
    print("addShadeFragmentFromBuffer returned:", repr(name))
    print("hasFragment(%s):" % fn, fm.hasFragment(fn))
    print("RESULT:", "OK_REGISTERED" if (name and fm.hasFragment(fn)) else "FAILED_INVALID_XML")

maya.standalone.uninitialize()
