# multiout_probe.py — 複数出力フラグメントの書式候補をヘッドレス登録で検証
import maya.standalone
maya.standalone.initialize(name="python")
import maya.api.OpenMayaRender as omr

fm = omr.MRenderer.getFragmentManager()

def try_xml(label, xml):
    name = fm.addShadeFragmentFromBuffer(xml.encode("utf-8"), False)
    print("%-28s -> registered=%r" % (label, name))

# 候補A: <outputs>に複数要素 + HLSL は struct 返却（メンバ名=出力名）
xmlA = '''<fragment uiName="vmtMO_A" name="vmtMO_A" type="plumbing" class="ShadeFragment" version="1.0">
  <description><![CDATA[A]]></description>
  <properties>
    <float2 name="uvCoord" semantic="mayaUvCoordSemantic" flags="varyingInputParam"/>
  </properties>
  <outputs>
    <float3 name="outColor"/>
    <float name="outHeight"/>
  </outputs>
  <implementation>
    <implementation render="OGSRenderer" language="HLSL" lang_version="11.0">
      <function_name val="vmtMO_A"/>
      <source><![CDATA[
struct vmtMO_A_t { float3 outColor; float outHeight; };
vmtMO_A_t vmtMO_A(float2 uvCoord){ vmtMO_A_t o; o.outColor=float3(uvCoord,0.0); o.outHeight=uvCoord.x; return o; }
]]></source>
    </implementation>
  </implementation>
</fragment>
'''

# 候補B: 出力を struct 型1個として宣言（struct name参照）
xmlB = '''<fragment uiName="vmtMO_B" name="vmtMO_B" type="plumbing" class="ShadeFragment" version="1.0">
  <description><![CDATA[B]]></description>
  <properties>
    <float2 name="uvCoord" semantic="mayaUvCoordSemantic" flags="varyingInputParam"/>
  </properties>
  <outputs>
    <struct name="result" struct_name="vmtMO_B_t"/>
  </outputs>
  <implementation>
    <implementation render="OGSRenderer" language="HLSL" lang_version="11.0">
      <function_name val="vmtMO_B"/>
      <source><![CDATA[
struct vmtMO_B_t { float3 outColor; float outHeight; };
vmtMO_B_t vmtMO_B(float2 uvCoord){ vmtMO_B_t o; o.outColor=float3(uvCoord,0.0); o.outHeight=uvCoord.x; return o; }
]]></source>
    </implementation>
  </implementation>
</fragment>
'''

# 候補C: out パラメータ方式
xmlC = '''<fragment uiName="vmtMO_C" name="vmtMO_C" type="plumbing" class="ShadeFragment" version="1.0">
  <description><![CDATA[C]]></description>
  <properties>
    <float2 name="uvCoord" semantic="mayaUvCoordSemantic" flags="varyingInputParam"/>
  </properties>
  <outputs>
    <float3 name="outColor"/>
    <float name="outHeight"/>
  </outputs>
  <implementation>
    <implementation render="OGSRenderer" language="HLSL" lang_version="11.0">
      <function_name val="vmtMO_C"/>
      <source><![CDATA[
void vmtMO_C(float2 uvCoord, out float3 outColor, out float outHeight){ outColor=float3(uvCoord,0.0); outHeight=uvCoord.x; }
]]></source>
    </implementation>
  </implementation>
</fragment>
'''

for lbl, x in [("A: multi <outputs>+struct", xmlA), ("B: <struct> output", xmlB), ("C: out-params", xmlC)]:
    try:
        try_xml(lbl, x)
    except Exception as e:
        print("%-28s -> EXC %s" % (lbl, e))

maya.standalone.uninitialize()
