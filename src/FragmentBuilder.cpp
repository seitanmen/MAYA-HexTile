// FragmentBuilder.cpp — VMT HexTile PoC
// Copyright (c) 2026 kawata / VMT.

#include <sstream>
#include <string>
#include <vector>
#include <set>

#include "FragmentBuilder.h"

#include <maya/MViewport2Renderer.h>
#include <maya/MFragmentManager.h>

namespace vmt {

MString hexTileFragmentName(int numColorMaps)
{
    std::ostringstream os;
    os << "VMTHexTilePoc_c" << numColorMaps;
    return MString(os.str().c_str());
}

// ---------------------------------------------------------------------------
// HLSL 実装本体（DX11 / Windows VP2 の既定バックエンド）
//   - frac / hash / TriangleGrid / makeCen / rot / tileUV は Nuke 版から移植
//   - 各カラーマップを 3 つの hex UV で SampleGrad（自動導関数）し W で合成
//   - PoC では N 枚の hex 結果を平均して 1 つの outColor に出す
// ---------------------------------------------------------------------------
static std::string hlslSource(int N, const std::string& fn)
{
    std::ostringstream s;

    // --- 共有ヘルパ ---
    s << R"(
float vmt_fracf(float x) { return x - floor(x); }

float2 vmt_hash(float2 p)
{
    float2 r = float2(dot(p, float2(127.1f, 311.7f)),
                      dot(p, float2(269.5f, 183.3f)));
    return frac(sin(r) * 43758.5453f);
}

void vmt_triGrid(float2 st, out float3 w, out float2 v1, out float2 v2, out float2 v3)
{
    const float scale = 2.0f * 1.7320508f;        // 2*sqrt(3)
    float2 s = st * scale;
    float skx = s.x - 0.57735027f * s.y;
    float sky = 1.15470054f * s.y;
    float2 b  = floor(float2(skx, sky));
    float fx = skx - b.x, fy = sky - b.y, fz = 1.0f - fx - fy;
    float sgn = (fz < 0.0f) ? 1.0f : 0.0f;
    float s2  = 2.0f * sgn - 1.0f;
    w  = float3(-fz * s2, sgn - fy * s2, sgn - fx * s2);
    v1 = b + float2(sgn, sgn);
    v2 = b + float2(sgn, 1.0f - sgn);
    v3 = b + float2(1.0f - sgn, sgn);
}

float2 vmt_makeCen(float2 v)
{
    const float invDenom = 1.0f / (2.0f * 1.7320508f);
    return float2((v.x + 0.5f * v.y) * invDenom, (0.8660254f * v.y) * invDenom);
}

void vmt_rot(float2 idx, float rotStr, out float ca, out float sa)
{
    const float PI = 3.14159265f, TAU = 6.28318531f;
    float a = abs(idx.x * idx.y) + abs(idx.x + idx.y) + PI;
    a = fmod(a, TAU);
    if (a < 0.0f) a += TAU;
    if (a > PI)   a -= TAU;
    a *= rotStr;
    ca = cos(a); sa = sin(a);
}

float2 vmt_tileUV(float2 st, float2 v, float rotStr)
{
    float2 cen = vmt_makeCen(v);
    float ca, sa; vmt_rot(v, rotStr, ca, sa);
    float2 h = vmt_hash(v);
    float2 d = st - cen;
    return float2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + cen + h;
}

// 1 枚のテクスチャを 3 つの hex UV で再サンプリングして合成（リスク③の核心）
float3 vmt_hexSample(Texture2D map, sampler smp, float2 uv, float tileScale, float rotStr)
{
    float2 st = uv * tileScale;
    float3 w; float2 v1, v2, v3;
    vmt_triGrid(st, w, v1, v2, v3);

    float2 uv1 = vmt_tileUV(st, v1, rotStr);
    float2 uv2 = vmt_tileUV(st, v2, rotStr);
    float2 uv3 = vmt_tileUV(st, v3, rotStr);

    // 回転後 UV の自動導関数を SampleGrad に渡す（AA / ミップ選択）
    float3 c1 = map.SampleGrad(smp, frac(uv1), ddx(uv1), ddy(uv1)).rgb;
    float3 c2 = map.SampleGrad(smp, frac(uv2), ddx(uv2), ddy(uv2)).rgb;
    float3 c3 = map.SampleGrad(smp, frac(uv3), ddx(uv3), ddy(uv3)).rgb;

    float3 W = pow(max(w, 0.0f.xxx), 7.0f);
    float sum = W.x + W.y + W.z;
    if (sum > 0.0f) W /= sum;

    return W.x * c1 + W.y * c2 + W.z * c3;
}
)";

    // --- エントリ関数（プロパティ順とシグネチャを一致させる）---
    s << "float3 " << fn << "(float2 uvCoord, float tileScale, float rotStrength";
    for (int i = 0; i < N; ++i)
        s << ", Texture2D map" << i << ", sampler map" << i << "Sampler";
    s << ")\n{\n";
    s << "    float3 acc = float3(0.0f, 0.0f, 0.0f);\n";
    for (int i = 0; i < N; ++i)
        s << "    acc += vmt_hexSample(map" << i << ", map" << i
          << "Sampler, uvCoord, tileScale, rotStrength);\n";
    if (N > 0)
        s << "    acc /= " << (float)N << "f;\n";
    s << "    return acc;\n}\n";

    return s.str();
}

// ---------------------------------------------------------------------------
// GLSL 実装（OpenGL VP2 / 参考・実機要調整）
//   ※ Windows 既定は DX11 のため HLSL が主。GLSL は best-effort。
// ---------------------------------------------------------------------------
static std::string glslSource(int N, const std::string& fn)
{
    std::ostringstream s;
    s << R"(
vec2 vmt_hash(vec2 p)
{
    vec2 r = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(r) * 43758.5453);
}
void vmt_triGrid(vec2 st, out vec3 w, out vec2 v1, out vec2 v2, out vec2 v3)
{
    const float scale = 2.0 * 1.7320508;
    vec2 s = st * scale;
    float skx = s.x - 0.57735027 * s.y;
    float sky = 1.15470054 * s.y;
    vec2 b = floor(vec2(skx, sky));
    float fx = skx - b.x, fy = sky - b.y, fz = 1.0 - fx - fy;
    float sgn = (fz < 0.0) ? 1.0 : 0.0;
    float s2 = 2.0 * sgn - 1.0;
    w  = vec3(-fz * s2, sgn - fy * s2, sgn - fx * s2);
    v1 = b + vec2(sgn, sgn);
    v2 = b + vec2(sgn, 1.0 - sgn);
    v3 = b + vec2(1.0 - sgn, sgn);
}
vec2 vmt_makeCen(vec2 v)
{
    const float invDenom = 1.0 / (2.0 * 1.7320508);
    return vec2((v.x + 0.5 * v.y) * invDenom, (0.8660254 * v.y) * invDenom);
}
void vmt_rot(vec2 idx, float rotStr, out float ca, out float sa)
{
    const float PI = 3.14159265, TAU = 6.28318531;
    float a = abs(idx.x * idx.y) + abs(idx.x + idx.y) + PI;
    a = mod(a, TAU); if (a > PI) a -= TAU;
    a *= rotStr; ca = cos(a); sa = sin(a);
}
vec2 vmt_tileUV(vec2 st, vec2 v, float rotStr)
{
    vec2 cen = vmt_makeCen(v);
    float ca, sa; vmt_rot(v, rotStr, ca, sa);
    vec2 h = vmt_hash(v);
    vec2 d = st - cen;
    return vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) + cen + h;
}
vec3 vmt_hexSample(sampler2D map, vec2 uv, float tileScale, float rotStr)
{
    vec2 st = uv * tileScale;
    vec3 w; vec2 v1, v2, v3;
    vmt_triGrid(st, w, v1, v2, v3);
    vec2 uv1 = vmt_tileUV(st, v1, rotStr);
    vec2 uv2 = vmt_tileUV(st, v2, rotStr);
    vec2 uv3 = vmt_tileUV(st, v3, rotStr);
    vec3 c1 = textureGrad(map, fract(uv1), dFdx(uv1), dFdy(uv1)).rgb;
    vec3 c2 = textureGrad(map, fract(uv2), dFdx(uv2), dFdy(uv2)).rgb;
    vec3 c3 = textureGrad(map, fract(uv3), dFdx(uv3), dFdy(uv3)).rgb;
    vec3 W = pow(max(w, vec3(0.0)), vec3(7.0));
    float sum = W.x + W.y + W.z;
    if (sum > 0.0) W /= sum;
    return W.x * c1 + W.y * c2 + W.z * c3;
}
)";
    s << "vec3 " << fn << "(vec2 uvCoord, float tileScale, float rotStrength";
    for (int i = 0; i < N; ++i)
        s << ", sampler2D map" << i;
    s << ")\n{\n    vec3 acc = vec3(0.0);\n";
    for (int i = 0; i < N; ++i)
        s << "    acc += vmt_hexSample(map" << i << ", uvCoord, tileScale, rotStrength);\n";
    if (N > 0)
        s << "    acc /= float(" << N << ");\n";
    s << "    return acc;\n}\n";
    return s.str();
}

// ---------------------------------------------------------------------------
// XML 組み立て
// ---------------------------------------------------------------------------
MString buildHexTileFragmentXML(int numColorMaps, const MString& fragmentName)
{
    const int N = (numColorMaps < 1) ? 1 : numColorMaps; // 最低 1
    const std::string fn = fragmentName.asChar();

    std::ostringstream xml;
    xml << "<fragment uiName=\"" << fn << "\" name=\"" << fn
        << "\" type=\"plumbing\" class=\"ShadeFragment\" version=\"1.0\">\n";
    xml << "  <description><![CDATA[VMT HexTile PoC — dynamic " << N
        << "-texture hex resampling]]></description>\n";

    // properties（プロパティの順序 = 関数引数の順序）
    xml << "  <properties>\n";
    xml << "    <float2 name=\"uvCoord\" semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"tileScale\"/>\n";
    xml << "    <float name=\"rotStrength\"/>\n";
    for (int i = 0; i < N; ++i) {
        xml << "    <texture2 name=\"map" << i << "\"/>\n";
        xml << "    <sampler name=\"map" << i << "Sampler\"/>\n";
    }
    xml << "  </properties>\n";

    // NOTE: <values>（既定値）は置かない。定数畳み込みで setParameter が
    // 効かなくなるため、値は updateShader から毎回 push する。

    // outputs（PoC は単一出力）
    xml << "  <outputs>\n";
    xml << "    <float3 name=\"outColor\"/>\n";
    xml << "  </outputs>\n";

    // implementation
    xml << "  <implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"HLSL\" lang_version=\"11.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << hlslSource(N, fn) << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"GLSL\" lang_version=\"3.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << glslSource(N, fn) << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "  </implementation>\n";

    xml << "</fragment>\n";

    return MString(xml.str().c_str());
}

// ---------------------------------------------------------------------------
// 診断用（v3）: テクスチャ無し・六角タイリングのパターン可視化フラグメント
// ---------------------------------------------------------------------------
MString hexVizFragmentName() { return MString("VMTHexTilePoc_viz"); }

static std::string vizHLSL(const std::string& fn)
{
    std::ostringstream s;
    s << R"(
float3 vmt_hash3(float2 p)
{
    float3 r = float3(dot(p, float2(127.1f, 311.7f)),
                      dot(p, float2(269.5f, 183.3f)),
                      dot(p, float2(419.2f, 371.9f)));
    return frac(sin(r) * 43758.5453f);
}
void vmt_triGrid(float2 st, out float3 w, out float2 v1, out float2 v2, out float2 v3)
{
    const float scale = 2.0f * 1.7320508f;
    float2 s = st * scale;
    float skx = s.x - 0.57735027f * s.y;
    float sky = 1.15470054f * s.y;
    float2 b  = floor(float2(skx, sky));
    float fx = skx - b.x, fy = sky - b.y, fz = 1.0f - fx - fy;
    float sgn = (fz < 0.0f) ? 1.0f : 0.0f;
    float s2  = 2.0f * sgn - 1.0f;
    w  = float3(-fz * s2, sgn - fy * s2, sgn - fx * s2);
    v1 = b + float2(sgn, sgn);
    v2 = b + float2(sgn, 1.0f - sgn);
    v3 = b + float2(1.0f - sgn, sgn);
}
)";
    // === v8 診断: パラメータ値を色で直接出力 ===
    // フラグメント側パラメータ名は uvScale / rotAmt にリネーム（"tileScale" 等の
    // 名前衝突回避の検証）。赤=uvScale(0..20→0..1)、緑=rotAmt、青=uvCoord.x。
    s << "float3 " << fn << "(float2 uvCoord, float uvScale, float rotAmt)\n{\n";
    s << "    return float3(saturate(uvScale * 0.05f), saturate(rotAmt), frac(uvCoord.x));\n}\n";
    return s.str();
}

static std::string vizGLSL(const std::string& fn)
{
    std::ostringstream s;
    s << R"(
vec3 vmt_hash3(vec2 p)
{
    vec3 r = vec3(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)), dot(p, vec2(419.2, 371.9)));
    return fract(sin(r) * 43758.5453);
}
void vmt_triGrid(vec2 st, out vec3 w, out vec2 v1, out vec2 v2, out vec2 v3)
{
    const float scale = 2.0 * 1.7320508;
    vec2 s = st * scale;
    float skx = s.x - 0.57735027 * s.y;
    float sky = 1.15470054 * s.y;
    vec2 b = floor(vec2(skx, sky));
    float fx = skx - b.x, fy = sky - b.y, fz = 1.0 - fx - fy;
    float sgn = (fz < 0.0) ? 1.0 : 0.0;
    float s2 = 2.0 * sgn - 1.0;
    w  = vec3(-fz * s2, sgn - fy * s2, sgn - fx * s2);
    v1 = b + vec2(sgn, sgn);
    v2 = b + vec2(sgn, 1.0 - sgn);
    v3 = b + vec2(1.0 - sgn, sgn);
}
)";
    // v8 診断: パラメータ値を色で直接出力（HLSL 版と同じ・リネーム済み）
    s << "vec3 " << fn << "(vec2 uvCoord, float uvScale, float rotAmt)\n{\n";
    s << "    return vec3(clamp(uvScale * 0.05, 0.0, 1.0), clamp(rotAmt, 0.0, 1.0), fract(uvCoord.x));\n}\n";
    return s.str();
}

MString buildHexVizFragmentXML(const MString& fragmentName)
{
    const std::string fn = fragmentName.asChar();
    std::ostringstream xml;
    xml << "<fragment uiName=\"" << fn << "\" name=\"" << fn
        << "\" type=\"plumbing\" class=\"ShadeFragment\" version=\"1.0\">\n";
    xml << "  <description><![CDATA[VMT HexTile PoC diagnostic — procedural hex pattern]]></description>\n";
    xml << "  <properties>\n";
    xml << "    <float2 name=\"uvCoord\" semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"uvScale\"/>\n";
    xml << "    <float name=\"rotAmt\"/>\n";
    xml << "  </properties>\n";
    // NOTE: <values> ブロックは置かない。値を入れるとパラメータが
    // コンパイル時定数に畳み込まれ、setParameter が効かなくなるため。
    xml << "  <outputs>\n    <float3 name=\"outColor\"/>\n  </outputs>\n";
    xml << "  <implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"HLSL\" lang_version=\"11.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << vizHLSL(fn) << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"GLSL\" lang_version=\"3.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << vizGLSL(fn) << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "  </implementation>\n";
    xml << "</fragment>\n";
    return MString(xml.str().c_str());
}

// ---------------------------------------------------------------------------
// 診断用（v10）: 接続テクスチャを現在UVでサンプリングするだけ
// ---------------------------------------------------------------------------
// normalMode: 0=なし, 1=OpenGL, 2=DirectX(Y反転)
MString texCheckFragmentName(bool decodeSrgb, bool useHeight, int normalMode)
{
    std::string n = "VMTHexTilePoc_tex_";
    n += decodeSrgb ? "srgb" : "lin";
    n += useHeight ? "_h1" : "_h0";
    n += (normalMode == 2) ? "_nD" : (normalMode == 1) ? "_nG" : "_n0";
    return MString(n.c_str());
}

MString buildTexCheckFragmentXML(const MString& fragmentName, bool decodeSrgb, bool useHeight, int normalMode)
{
    const std::string fn = fragmentName.asChar();
    const std::string dec  = decodeSrgb ? "    c1=vmt_s2l(c1); c2=vmt_s2l(c2); c3=vmt_s2l(c3);\n" : "";
    const bool useNormal = (normalMode != 0);
    const std::string FY = (normalMode == 2) ? "-1.0" : "1.0"; // DirectX は Y 反転

    // 共通 HLSL ヘルパ（+ height 用）
    std::string hH =
"float2 vmt_hash(float2 p){float2 r=float2(dot(p,float2(127.1,311.7)),dot(p,float2(269.5,183.3)));return frac(sin(r)*43758.5453);}\n"
"void vmt_tri(float2 st,out float3 w,out float2 v1,out float2 v2,out float2 v3){const float sc=2.0*1.7320508;float2 s=st*sc;float skx=s.x-0.57735027*s.y;float sky=1.15470054*s.y;float2 b=floor(float2(skx,sky));float fx=skx-b.x,fy=sky-b.y,fz=1.0-fx-fy;float g=(fz<0.0)?1.0:0.0;float s2=2.0*g-1.0;w=float3(-fz*s2,g-fy*s2,g-fx*s2);v1=b+float2(g,g);v2=b+float2(g,1.0-g);v3=b+float2(1.0-g,g);}\n"
"float2 vmt_cen(float2 v){const float id=1.0/(2.0*1.7320508);return float2((v.x+0.5*v.y)*id,(0.8660254*v.y)*id);}\n"
"void vmt_rot(float2 ix,float rs,out float ca,out float sa){const float PI=3.14159265,TAU=6.28318531;float a=abs(ix.x*ix.y)+abs(ix.x+ix.y)+PI;a=fmod(a,TAU);if(a<0.0)a+=TAU;if(a>PI)a-=TAU;a*=rs;ca=cos(a);sa=sin(a);}\n"
"float2 vmt_tuv(float2 st,float2 v,float rs){float2 c=vmt_cen(v);float ca,sa;vmt_rot(v,rs,ca,sa);float2 h=vmt_hash(v);float2 d=st-c;return float2(ca*d.x-sa*d.y,sa*d.x+ca*d.y)+c+h;}\n"
"float3 vmt_smp(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).rgb;}\n"
"float3 vmt_s2l(float3 c){return float3(c.x<=0.04045?c.x/12.92:pow((c.x+0.055)/1.055,2.4),c.y<=0.04045?c.y/12.92:pow((c.y+0.055)/1.055,2.4),c.z<=0.04045?c.z/12.92:pow((c.z+0.055)/1.055,2.4));}\n";
    if (useHeight) hH +=
"float vmt_smpA(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).r;}\n"
"float vmt_sth(float a,float th,float dl){dl=max(dl,0.0001);return clamp((a-th+dl)/dl,0.0,1.0);}\n"
"float vmt_stw(float a,float b,float c,float dl){return min(vmt_sth(a,b,dl),vmt_sth(a,c,dl));}\n";
    if (useNormal) hH +=
"float3 vmt_smpN(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).rgb*2.0-1.0;}\n";

    const std::string hSig = useHeight
        ? ", float heightWeight, float heightDelta, Texture2D map1, sampler map1Sampler" : "";
    // normal: map2/map2Sampler を追加
    const std::string hSigN = useNormal ? ", Texture2D map2, sampler map2Sampler" : "";
    // normal 合成（共有W・各タイル回転・derivative空間ブレンド・再正規化・OpenGLエンコード出力）
    const std::string hNrm = useNormal ?
        "    float3 N1=vmt_smpN(map2,map2Sampler,u1),N2=vmt_smpN(map2,map2Sampler,u2),N3=vmt_smpN(map2,map2Sampler,u3);\n"
        "    N1.y*=" + FY + "; N2.y*=" + FY + "; N3.y*=" + FY + ";\n"
        "    float nc1,ns1,nc2,ns2,nc3,ns3; vmt_rot(v1,rotAmt,nc1,ns1); vmt_rot(v2,rotAmt,nc2,ns2); vmt_rot(v3,rotAmt,nc3,ns3);\n"
        "    N1.xy=float2(nc1*N1.x-ns1*N1.y, ns1*N1.x+nc1*N1.y);\n"
        "    N2.xy=float2(nc2*N2.x-ns2*N2.y, ns2*N2.x+nc2*N2.y);\n"
        "    N3.xy=float2(nc3*N3.x-ns3*N3.y, ns3*N3.x+nc3*N3.y);\n"
        "    float2 ng1=-N1.xy/max(N1.z,0.001),ng2=-N2.xy/max(N2.z,0.001),ng3=-N3.xy/max(N3.z,0.001);\n"
        "    float2 nG=W.x*ng1+W.y*ng2+W.z*ng3;\n"
        "    float3 nrm=normalize(float3(-nG,1.0))*0.5+0.5;\n"
      :
        "    float3 nrm=float3(0.5,0.5,1.0);\n";
    const std::string hW = useHeight ?
        "    float h1=vmt_smpA(map1,map1Sampler,u1),h2=vmt_smpA(map1,map1Sampler,u2),h3=vmt_smpA(map1,map1Sampler,u3);\n"
        "    float r1=w.x+heightWeight*h1,r2=w.y+heightWeight*h2,r3=w.z+heightWeight*h3;\n"
        "    float dl=heightDelta*0.70710678;\n"
        "    float3 W=float3(vmt_stw(r1,r2,r3,dl),vmt_stw(r2,r1,r3,dl),vmt_stw(r3,r1,r2,dl));\n"
        "    float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n"
      :
        "    float3 W=pow(max(w,float3(0,0,0)),float3(7,7,7)); float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n";

    // 複数出力は struct 返却。struct 型 vmtHexOut は structure フラグメントで宣言し、
    // fragment_graph 経由で本フラグメントの出力に結線する（channels.xml と同型）。
    const std::string outH = useHeight ? "W.x*h1+W.y*h2+W.z*h3" : "0.0";
    std::string hlsl = hH +
        "vmtHexOut " + fn + "(float2 uvCoord, float uvScale, float rotAmt, Texture2D map0, sampler map0Sampler" + hSig + hSigN + ")\n"
        "{\n"
        "    float2 st = uvCoord * uvScale;\n"
        "    float3 w; float2 v1,v2,v3; vmt_tri(st,w,v1,v2,v3);\n"
        "    float2 u1=vmt_tuv(st,v1,rotAmt), u2=vmt_tuv(st,v2,rotAmt), u3=vmt_tuv(st,v3,rotAmt);\n"
        "    float3 c1=vmt_smp(map0,map0Sampler,u1), c2=vmt_smp(map0,map0Sampler,u2), c3=vmt_smp(map0,map0Sampler,u3);\n"
        + dec + hW + hNrm +
        "    vmtHexOut o; o.outColor = W.x*c1 + W.y*c2 + W.z*c3; o.outHeight = " + outH + "; o.outNormal = nrm; return o;\n"
        "}\n";

    // 共通 GLSL ヘルパ（+ height 用）
    std::string gH =
"vec2 vmt_hash(vec2 p){vec2 r=vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3)));return fract(sin(r)*43758.5453);}\n"
"void vmt_tri(vec2 st,out vec3 w,out vec2 v1,out vec2 v2,out vec2 v3){const float sc=2.0*1.7320508;vec2 s=st*sc;float skx=s.x-0.57735027*s.y;float sky=1.15470054*s.y;vec2 b=floor(vec2(skx,sky));float fx=skx-b.x,fy=sky-b.y,fz=1.0-fx-fy;float g=(fz<0.0)?1.0:0.0;float s2=2.0*g-1.0;w=vec3(-fz*s2,g-fy*s2,g-fx*s2);v1=b+vec2(g,g);v2=b+vec2(g,1.0-g);v3=b+vec2(1.0-g,g);}\n"
"vec2 vmt_cen(vec2 v){const float id=1.0/(2.0*1.7320508);return vec2((v.x+0.5*v.y)*id,(0.8660254*v.y)*id);}\n"
"void vmt_rot(vec2 ix,float rs,out float ca,out float sa){const float PI=3.14159265,TAU=6.28318531;float a=abs(ix.x*ix.y)+abs(ix.x+ix.y)+PI;a=mod(a,TAU);if(a>PI)a-=TAU;a*=rs;ca=cos(a);sa=sin(a);}\n"
"vec2 vmt_tuv(vec2 st,vec2 v,float rs){vec2 c=vmt_cen(v);float ca,sa;vmt_rot(v,rs,ca,sa);vec2 h=vmt_hash(v);vec2 d=st-c;return vec2(ca*d.x-sa*d.y,sa*d.x+ca*d.y)+c+h;}\n"
"vec3 vmt_smp(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).rgb;}\n"
"vec3 vmt_s2l(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}\n";
    if (useHeight) gH +=
"float vmt_smpA(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).r;}\n"
"float vmt_sth(float a,float th,float dl){dl=max(dl,0.0001);return clamp((a-th+dl)/dl,0.0,1.0);}\n"
"float vmt_stw(float a,float b,float c,float dl){return min(vmt_sth(a,b,dl),vmt_sth(a,c,dl));}\n";
    if (useNormal) gH +=
"vec3 vmt_smpN(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).rgb*2.0-1.0;}\n";

    const std::string gSig = useHeight
        ? ", float heightWeight, float heightDelta, sampler2D map1" : "";
    const std::string gSigN = useNormal ? ", sampler2D map2" : "";
    const std::string gNrm = useNormal ?
        "    vec3 N1=vmt_smpN(map2,u1),N2=vmt_smpN(map2,u2),N3=vmt_smpN(map2,u3);\n"
        "    N1.y*=" + FY + "; N2.y*=" + FY + "; N3.y*=" + FY + ";\n"
        "    float nc1,ns1,nc2,ns2,nc3,ns3; vmt_rot(v1,rotAmt,nc1,ns1); vmt_rot(v2,rotAmt,nc2,ns2); vmt_rot(v3,rotAmt,nc3,ns3);\n"
        "    N1.xy=vec2(nc1*N1.x-ns1*N1.y, ns1*N1.x+nc1*N1.y);\n"
        "    N2.xy=vec2(nc2*N2.x-ns2*N2.y, ns2*N2.x+nc2*N2.y);\n"
        "    N3.xy=vec2(nc3*N3.x-ns3*N3.y, ns3*N3.x+nc3*N3.y);\n"
        "    vec2 ng1=-N1.xy/max(N1.z,0.001),ng2=-N2.xy/max(N2.z,0.001),ng3=-N3.xy/max(N3.z,0.001);\n"
        "    vec2 nG=W.x*ng1+W.y*ng2+W.z*ng3;\n"
        "    vec3 nrm=normalize(vec3(-nG,1.0))*0.5+0.5;\n"
      :
        "    vec3 nrm=vec3(0.5,0.5,1.0);\n";
    const std::string gW = useHeight ?
        "    float h1=vmt_smpA(map1,u1),h2=vmt_smpA(map1,u2),h3=vmt_smpA(map1,u3);\n"
        "    float r1=w.x+heightWeight*h1,r2=w.y+heightWeight*h2,r3=w.z+heightWeight*h3;\n"
        "    float dl=heightDelta*0.70710678;\n"
        "    vec3 W=vec3(vmt_stw(r1,r2,r3,dl),vmt_stw(r2,r1,r3,dl),vmt_stw(r3,r1,r2,dl));\n"
        "    float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n"
      :
        "    vec3 W=pow(max(w,vec3(0.0)),vec3(7.0)); float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n";

    std::string glsl = gH +
        "vmtHexOut " + fn + "(vec2 uvCoord, float uvScale, float rotAmt, sampler2D map0" + gSig + gSigN + ")\n"
        "{\n"
        "    vec2 st = uvCoord * uvScale;\n"
        "    vec3 w; vec2 v1,v2,v3; vmt_tri(st,w,v1,v2,v3);\n"
        "    vec2 u1=vmt_tuv(st,v1,rotAmt), u2=vmt_tuv(st,v2,rotAmt), u3=vmt_tuv(st,v3,rotAmt);\n"
        "    vec3 c1=vmt_smp(map0,u1), c2=vmt_smp(map0,u2), c3=vmt_smp(map0,u3);\n"
        + dec + gW + gNrm +
        "    vmtHexOut o; o.outColor = W.x*c1+W.y*c2+W.z*c3; o.outHeight = " + outH + "; o.outNormal = nrm; return o;\n"
        "}\n";

    std::ostringstream xml;
    xml << "<fragment uiName=\"" << fn << "\" name=\"" << fn
        << "\" type=\"plumbing\" class=\"ShadeFragment\" version=\"1.0\">\n";
    xml << "  <description><![CDATA[VMT hex tiling of a texture]]></description>\n";
    xml << "  <properties>\n";
    xml << "    <float2 name=\"uvCoord\" semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"uvScale\"/>\n";
    xml << "    <float name=\"rotAmt\"/>\n";
    xml << "    <texture2 name=\"map0\"/>\n";
    xml << "    <sampler name=\"map0Sampler\"/>\n";
    if (useHeight) {
        xml << "    <float name=\"heightWeight\"/>\n";
        xml << "    <float name=\"heightDelta\"/>\n";
        xml << "    <texture2 name=\"map1\"/>\n";
        xml << "    <sampler name=\"map1Sampler\"/>\n";
    }
    if (useNormal) {
        xml << "    <texture2 name=\"map2\"/>\n";
        xml << "    <sampler name=\"map2Sampler\"/>\n";
    }
    xml << "  </properties>\n";
    xml << "  <outputs>\n";
    xml << "    <struct name=\"output\" struct_name=\"vmtHexOut\"/>\n";
    xml << "  </outputs>\n";
    xml << "  <implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"HLSL\" lang_version=\"11.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << hlsl << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"GLSL\" lang_version=\"3.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n";
    xml << "      <source><![CDATA[\n" << glsl << "\n]]></source>\n";
    xml << "    </implementation>\n";
    xml << "  </implementation>\n";
    xml << "</fragment>\n";
    return MString(xml.str().c_str());
}

// ---------------------------------------------------------------------------
// 複数出力 struct 型 vmtHexOut を宣言する structure フラグメント。
//   （mayaStandardSurfaceParams.xml と同型。グラフ内で leaf の struct 出力に結線。）
//   メンバ: outColor(float3), outHeight(float) — outputForConnection が参照する名前。
// ---------------------------------------------------------------------------
MString vmtHexStructFragmentName() { return MString("vmtHexOut"); }

MString buildVmtHexStructXML()
{
    const char* declHLSL =
        "struct vmtHexOut {\n    float3 outColor;\n    float outHeight;\n    float3 outNormal;\n};\n";
    const char* declGLSL =
        "struct vmtHexOut {\n    vec3 outColor;\n    float outHeight;\n    vec3 outNormal;\n};\n";

    std::ostringstream xml;
    xml << "<fragment uiName=\"vmtHexOut\" name=\"vmtHexOut\" type=\"structure\""
           " class=\"ShadeFragment\" version=\"1.0\" feature_level=\"0\">\n";
    xml << "  <description><![CDATA[VMT HexTile multi-output struct]]></description>\n";
    xml << "  <properties>\n";
    xml << "    <struct name=\"vmtHexOut\" struct_name=\"vmtHexOut\"/>\n";
    xml << "  </properties>\n";
    xml << "  <values>\n  </values>\n";
    xml << "  <outputs>\n";
    xml << "    <alias name=\"vmtHexOut\" struct_name=\"vmtHexOut\"/>\n";
    xml << "    <float3 name=\"outColor\"/>\n";
    xml << "    <float name=\"outHeight\"/>\n";
    xml << "    <float3 name=\"outNormal\"/>\n";
    xml << "  </outputs>\n";
    xml << "  <implementation>\n";
    // Cg / HLSL / GLSL いずれも struct 宣言のみ（function_name は空）。
    xml << "    <implementation render=\"OGSRenderer\" language=\"Cg\" lang_version=\"2.1\">\n";
    xml << "      <function_name val=\"\"/>\n";
    xml << "      <declaration name=\"vmtHexOut\"><![CDATA[\n" << declHLSL << "]]></declaration>\n";
    xml << "    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"HLSL\" lang_version=\"11.0\">\n";
    xml << "      <function_name val=\"\"/>\n";
    xml << "      <declaration name=\"vmtHexOut\"><![CDATA[\n" << declHLSL << "]]></declaration>\n";
    xml << "    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"GLSL\" lang_version=\"3.0\">\n";
    xml << "      <function_name val=\"\"/>\n";
    xml << "      <declaration name=\"vmtHexOut\"><![CDATA[\n" << declGLSL << "]]></declaration>\n";
    xml << "    </implementation>\n";
    xml << "  </implementation>\n";
    xml << "</fragment>\n";
    return MString(xml.str().c_str());
}

// ---------------------------------------------------------------------------
// leaf(compute) + structure(vmtHexOut) を束ねる fragment_graph。
//   override の fragmentName() はこのグラフ名を返す。
//   graphName : 公開フラグメント名（= texCheckFragmentName）
//   implName  : leaf フラグメント名（= graphName + "_impl"）
// ---------------------------------------------------------------------------
MString texCheckImplName(bool decodeSrgb, bool useHeight, int normalMode)
{
    return texCheckFragmentName(decodeSrgb, useHeight, normalMode) + "_impl";
}

MString buildTexCheckGraphXML(const MString& graphName, const MString& implName, bool useHeight, int normalMode)
{
    const bool useNormal = (normalMode != 0);
    const std::string g = graphName.asChar();
    const std::string im = implName.asChar();

    std::ostringstream xml;
    xml << "<fragment_graph name=\"" << g << "\" ref=\"" << g
        << "\" class=\"FragmentGraph\" version=\"1.0\">\n";
    xml << "  <fragments>\n";
    xml << "    <fragment_ref name=\"vmtHexOut\" ref=\"vmtHexOut\"/>\n";
    xml << "    <fragment_ref name=\"" << im << "\" ref=\"" << im << "\"/>\n";
    xml << "  </fragments>\n";
    xml << "  <connections>\n";
    xml << "    <connect from=\"" << im << ".output\" to=\"vmtHexOut.vmtHexOut\" name=\"vmtHexOut\"/>\n";
    xml << "  </connections>\n";
    // leaf の入力プロパティをグラフレベルに公開（名前は getCustomMappings と一致）。
    xml << "  <properties>\n";
    // uvCoord は varying セマンティックをグラフレベルにも明示（無いと UV が定数化し単色になる）。
    xml << "    <float2 name=\"uvCoord\" ref=\"" << im << ".uvCoord\""
           " semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"uvScale\" ref=\"" << im << ".uvScale\"/>\n";
    xml << "    <float name=\"rotAmt\" ref=\"" << im << ".rotAmt\"/>\n";
    xml << "    <texture2 name=\"map0\" ref=\"" << im << ".map0\"/>\n";
    xml << "    <sampler name=\"map0Sampler\" ref=\"" << im << ".map0Sampler\"/>\n";
    if (useHeight) {
        xml << "    <float name=\"heightWeight\" ref=\"" << im << ".heightWeight\"/>\n";
        xml << "    <float name=\"heightDelta\" ref=\"" << im << ".heightDelta\"/>\n";
        xml << "    <texture2 name=\"map1\" ref=\"" << im << ".map1\"/>\n";
        xml << "    <sampler name=\"map1Sampler\" ref=\"" << im << ".map1Sampler\"/>\n";
    }
    if (useNormal) {
        xml << "    <texture2 name=\"map2\" ref=\"" << im << ".map2\"/>\n";
        xml << "    <sampler name=\"map2Sampler\" ref=\"" << im << ".map2Sampler\"/>\n";
    }
    xml << "  </properties>\n";
    // NOTE: <values> は置かない（スカラの定数畳み込み回避。値は rebuild/setParameter で push）。
    xml << "  <outputs>\n";
    xml << "    <struct name=\"vmtHexOut\" ref=\"vmtHexOut.vmtHexOut\"/>\n";
    xml << "  </outputs>\n";
    xml << "</fragment_graph>\n";
    return MString(xml.str().c_str());
}

// ===========================================================================
// 動的フラグメント生成（真の無制限）
//   接続シグネチャ = colorMaps 枚数 N ＋ 各マップの sRGB ビットマスク
//                    ＋ height 有無 ＋ normalMode(0/1/2)
//   から専用の struct / leaf / graph を実行時生成し、未登録なら登録する。
//   texture 名: 色 = map0..map{N-1}, height = mapH, normal = mapNrm。
//   struct vmtHexOut_c{N} { float3 outColor0..{N-1}; float outHeight; float3 outNormal; }
// ===========================================================================

// 登録済み動的フラグメント名（uninit で一括解除するため記録）。
static std::set<std::string>& dynRegistry()
{
    static std::set<std::string> s;
    return s;
}

static std::string hlslHelpers(bool useHeight, bool useNormal)
{
    std::string h =
"float2 vmt_hash(float2 p){float2 r=float2(dot(p,float2(127.1,311.7)),dot(p,float2(269.5,183.3)));return frac(sin(r)*43758.5453);}\n"
"void vmt_tri(float2 st,out float3 w,out float2 v1,out float2 v2,out float2 v3){const float sc=2.0*1.7320508;float2 s=st*sc;float skx=s.x-0.57735027*s.y;float sky=1.15470054*s.y;float2 b=floor(float2(skx,sky));float fx=skx-b.x,fy=sky-b.y,fz=1.0-fx-fy;float g=(fz<0.0)?1.0:0.0;float s2=2.0*g-1.0;w=float3(-fz*s2,g-fy*s2,g-fx*s2);v1=b+float2(g,g);v2=b+float2(g,1.0-g);v3=b+float2(1.0-g,g);}\n"
"float2 vmt_cen(float2 v){const float id=1.0/(2.0*1.7320508);return float2((v.x+0.5*v.y)*id,(0.8660254*v.y)*id);}\n"
"void vmt_rot(float2 ix,float rs,out float ca,out float sa){const float PI=3.14159265,TAU=6.28318531;float a=abs(ix.x*ix.y)+abs(ix.x+ix.y)+PI;a=fmod(a,TAU);if(a<0.0)a+=TAU;if(a>PI)a-=TAU;a*=rs;ca=cos(a);sa=sin(a);}\n"
"float2 vmt_tuv(float2 st,float2 v,float rs){float2 c=vmt_cen(v);float ca,sa;vmt_rot(v,rs,ca,sa);float2 h=vmt_hash(v);float2 d=st-c;return float2(ca*d.x-sa*d.y,sa*d.x+ca*d.y)+c+h;}\n"
"float3 vmt_smp(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).rgb;}\n"
"float3 vmt_s2l(float3 c){return float3(c.x<=0.04045?c.x/12.92:pow((c.x+0.055)/1.055,2.4),c.y<=0.04045?c.y/12.92:pow((c.y+0.055)/1.055,2.4),c.z<=0.04045?c.z/12.92:pow((c.z+0.055)/1.055,2.4));}\n";
    if (useHeight) h +=
"float vmt_smpA(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).r;}\n"
"float vmt_sth(float a,float th,float dl){dl=max(dl,0.0001);return clamp((a-th+dl)/dl,0.0,1.0);}\n"
"float vmt_stw(float a,float b,float c,float dl){return min(vmt_sth(a,b,dl),vmt_sth(a,c,dl));}\n";
    if (useNormal) h +=
"float3 vmt_smpN(Texture2D m,sampler s,float2 uv){float2 f=frac(uv);return m.SampleGrad(s,float2(f.x,1.0-f.y),ddx(uv),ddy(uv)).rgb*2.0-1.0;}\n";
    return h;
}

static std::string glslHelpers(bool useHeight, bool useNormal)
{
    std::string h =
"vec2 vmt_hash(vec2 p){vec2 r=vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3)));return fract(sin(r)*43758.5453);}\n"
"void vmt_tri(vec2 st,out vec3 w,out vec2 v1,out vec2 v2,out vec2 v3){const float sc=2.0*1.7320508;vec2 s=st*sc;float skx=s.x-0.57735027*s.y;float sky=1.15470054*s.y;vec2 b=floor(vec2(skx,sky));float fx=skx-b.x,fy=sky-b.y,fz=1.0-fx-fy;float g=(fz<0.0)?1.0:0.0;float s2=2.0*g-1.0;w=vec3(-fz*s2,g-fy*s2,g-fx*s2);v1=b+vec2(g,g);v2=b+vec2(g,1.0-g);v3=b+vec2(1.0-g,g);}\n"
"vec2 vmt_cen(vec2 v){const float id=1.0/(2.0*1.7320508);return vec2((v.x+0.5*v.y)*id,(0.8660254*v.y)*id);}\n"
"void vmt_rot(vec2 ix,float rs,out float ca,out float sa){const float PI=3.14159265,TAU=6.28318531;float a=abs(ix.x*ix.y)+abs(ix.x+ix.y)+PI;a=mod(a,TAU);if(a>PI)a-=TAU;a*=rs;ca=cos(a);sa=sin(a);}\n"
"vec2 vmt_tuv(vec2 st,vec2 v,float rs){vec2 c=vmt_cen(v);float ca,sa;vmt_rot(v,rs,ca,sa);vec2 h=vmt_hash(v);vec2 d=st-c;return vec2(ca*d.x-sa*d.y,sa*d.x+ca*d.y)+c+h;}\n"
"vec3 vmt_smp(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).rgb;}\n"
"vec3 vmt_s2l(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));}\n";
    if (useHeight) h +=
"float vmt_smpA(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).r;}\n"
"float vmt_sth(float a,float th,float dl){dl=max(dl,0.0001);return clamp((a-th+dl)/dl,0.0,1.0);}\n"
"float vmt_stw(float a,float b,float c,float dl){return min(vmt_sth(a,b,dl),vmt_sth(a,c,dl));}\n";
    if (useNormal) h +=
"vec3 vmt_smpN(sampler2D m,vec2 uv){vec2 f=fract(uv);return textureGrad(m,vec2(f.x,1.0-f.y),dFdx(uv),dFdy(uv)).rgb*2.0-1.0;}\n";
    return h;
}

// 固定 8 スロット。struct は単一（vmtHexOut8）。
MString dynStructName() { return MString("vmtHexOut8"); }

// シグネチャ = connMask(接続スロット 8bit) + srgbMask(8bit) + height + normalMode。
MString dynFragmentName(unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode)
{
    std::ostringstream s;
    s << "VMTHexTilePoc_d_c" << std::hex << (connMask & 0xFF)
      << "_s" << (srgbMask & 0xFF) << std::dec
      << "_h" << (useHeight ? 1 : 0) << "_n" << normalMode;
    return MString(s.str().c_str());
}

// 固定 8 出力 struct（outColor0..7 + outHeight + outNormal + outColorNull）。
MString buildDynStructXML()
{
    const std::string sn = dynStructName().asChar();
    std::ostringstream dH, dG;
    dH << "struct " << sn << " {\n";
    dG << "struct " << sn << " {\n";
    for (int i = 0; i < kFixedColorSlots; ++i) { dH << "    float3 outColor" << i << ";\n"; dG << "    vec3 outColor" << i << ";\n"; }
    dH << "    float outHeight;\n    float3 outNormal;\n    float3 outColorNull;\n};\n";
    dG << "    float outHeight;\n    vec3 outNormal;\n    vec3 outColorNull;\n};\n";

    std::ostringstream xml;
    xml << "<fragment uiName=\"" << sn << "\" name=\"" << sn << "\" type=\"structure\""
           " class=\"ShadeFragment\" version=\"1.0\" feature_level=\"0\">\n";
    xml << "  <description><![CDATA[VMT HexTile multi-output struct (8 fixed colors)]]></description>\n";
    xml << "  <properties>\n    <struct name=\"" << sn << "\" struct_name=\"" << sn << "\"/>\n  </properties>\n";
    xml << "  <values>\n  </values>\n";
    xml << "  <outputs>\n    <alias name=\"" << sn << "\" struct_name=\"" << sn << "\"/>\n";
    for (int i = 0; i < kFixedColorSlots; ++i) xml << "    <float3 name=\"outColor" << i << "\"/>\n";
    xml << "    <float name=\"outHeight\"/>\n    <float3 name=\"outNormal\"/>\n    <float3 name=\"outColorNull\"/>\n  </outputs>\n";
    xml << "  <implementation>\n";
    const char* langs[3] = {"Cg", "HLSL", "GLSL"};
    const char* vers[3]  = {"2.1", "11.0", "3.0"};
    for (int L = 0; L < 3; ++L) {
        xml << "    <implementation render=\"OGSRenderer\" language=\"" << langs[L]
            << "\" lang_version=\"" << vers[L] << "\">\n";
        xml << "      <function_name val=\"\"/>\n";
        xml << "      <declaration name=\"" << sn << "\"><![CDATA[\n"
            << ((L == 2) ? dG.str() : dH.str()) << "]]></declaration>\n";
        xml << "    </implementation>\n";
    }
    xml << "  </implementation>\n</fragment>\n";
    return MString(xml.str().c_str());
}

MString buildDynLeafXML(const MString& implName, unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode)
{
    const std::string fn = implName.asChar();
    const std::string sn = dynStructName().asChar();
    const bool useNormal = (normalMode != 0);
    const std::string FY = (normalMode == 2) ? "-1.0" : "1.0";
    auto conn = [&](int i){ return (connMask & (1u << i)) != 0; };

    // ---- HLSL ----
    std::ostringstream hSig;
    hSig << "float2 uvCoord, float uvScale, float rotAmt";
    for (int i = 0; i < kFixedColorSlots; ++i) if (conn(i)) hSig << ", Texture2D map" << i << ", sampler map" << i << "Sampler";
    if (useHeight) hSig << ", float heightWeight, float heightDelta, Texture2D mapH, sampler mapHSampler";
    if (useNormal) hSig << ", Texture2D mapNrm, sampler mapNrmSampler";

    std::string hW = useHeight ?
        "    float h1=vmt_smpA(mapH,mapHSampler,u1),h2=vmt_smpA(mapH,mapHSampler,u2),h3=vmt_smpA(mapH,mapHSampler,u3);\n"
        "    float r1=w.x+heightWeight*h1,r2=w.y+heightWeight*h2,r3=w.z+heightWeight*h3;\n"
        "    float dl=heightDelta*0.70710678;\n"
        "    float3 W=float3(vmt_stw(r1,r2,r3,dl),vmt_stw(r2,r1,r3,dl),vmt_stw(r3,r1,r2,dl));\n"
        "    float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n"
      : "    float3 W=pow(max(w,float3(0,0,0)),float3(7,7,7)); float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n";
    std::string hNrm = useNormal ?
        "    float3 N1=vmt_smpN(mapNrm,mapNrmSampler,u1),N2=vmt_smpN(mapNrm,mapNrmSampler,u2),N3=vmt_smpN(mapNrm,mapNrmSampler,u3);\n"
        "    N1.y*=" + FY + "; N2.y*=" + FY + "; N3.y*=" + FY + ";\n"
        "    float nc1,ns1,nc2,ns2,nc3,ns3; vmt_rot(v1,rotAmt,nc1,ns1); vmt_rot(v2,rotAmt,nc2,ns2); vmt_rot(v3,rotAmt,nc3,ns3);\n"
        "    N1.xy=float2(nc1*N1.x-ns1*N1.y, ns1*N1.x+nc1*N1.y);\n"
        "    N2.xy=float2(nc2*N2.x-ns2*N2.y, ns2*N2.x+nc2*N2.y);\n"
        "    N3.xy=float2(nc3*N3.x-ns3*N3.y, ns3*N3.x+nc3*N3.y);\n"
        "    float2 ng1=-N1.xy/max(N1.z,0.001),ng2=-N2.xy/max(N2.z,0.001),ng3=-N3.xy/max(N3.z,0.001);\n"
        "    float2 nG=W.x*ng1+W.y*ng2+W.z*ng3;\n"
        "    float3 nrm=normalize(float3(-nG,1.0))*0.5+0.5;\n"
      : "    float3 nrm=float3(0.5,0.5,1.0);\n";

    std::ostringstream hb;
    hb << sn << " " << fn << "(" << hSig.str() << ")\n{\n";
    hb << "    float2 st = uvCoord*uvScale;\n";
    hb << "    float3 w; float2 v1,v2,v3; vmt_tri(st,w,v1,v2,v3);\n";
    hb << "    float2 u1=vmt_tuv(st,v1,rotAmt), u2=vmt_tuv(st,v2,rotAmt), u3=vmt_tuv(st,v3,rotAmt);\n";
    hb << hW << hNrm;
    hb << "    " << sn << " o;\n";
    for (int i = 0; i < kFixedColorSlots; ++i) {
        if (conn(i)) {
            hb << "    float3 c" << i << "1=vmt_smp(map" << i << ",map" << i << "Sampler,u1),c" << i
               << "2=vmt_smp(map" << i << ",map" << i << "Sampler,u2),c" << i
               << "3=vmt_smp(map" << i << ",map" << i << "Sampler,u3);\n";
            if (srgbMask & (1u << i))
                hb << "    c" << i << "1=vmt_s2l(c" << i << "1);c" << i << "2=vmt_s2l(c" << i << "2);c" << i << "3=vmt_s2l(c" << i << "3);\n";
            hb << "    o.outColor" << i << "=W.x*c" << i << "1+W.y*c" << i << "2+W.z*c" << i << "3;\n";
        } else {
            hb << "    o.outColor" << i << "=float3(0,0,0);\n"; // 未接続スロットは黒
        }
    }
    hb << "    o.outHeight=" << (useHeight ? "W.x*h1+W.y*h2+W.z*h3" : "0.0") << ";\n";
    hb << "    o.outNormal=nrm;\n    o.outColorNull=float3(0,0,0);\n    return o;\n}\n";
    std::string hlsl = hlslHelpers(useHeight, useNormal) + hb.str();

    // ---- GLSL ----
    std::ostringstream gSig;
    gSig << "vec2 uvCoord, float uvScale, float rotAmt";
    for (int i = 0; i < kFixedColorSlots; ++i) if (conn(i)) gSig << ", sampler2D map" << i;
    if (useHeight) gSig << ", float heightWeight, float heightDelta, sampler2D mapH";
    if (useNormal) gSig << ", sampler2D mapNrm";

    std::string gW = useHeight ?
        "    float h1=vmt_smpA(mapH,u1),h2=vmt_smpA(mapH,u2),h3=vmt_smpA(mapH,u3);\n"
        "    float r1=w.x+heightWeight*h1,r2=w.y+heightWeight*h2,r3=w.z+heightWeight*h3;\n"
        "    float dl=heightDelta*0.70710678;\n"
        "    vec3 W=vec3(vmt_stw(r1,r2,r3,dl),vmt_stw(r2,r1,r3,dl),vmt_stw(r3,r1,r2,dl));\n"
        "    float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n"
      : "    vec3 W=pow(max(w,vec3(0.0)),vec3(7.0)); float sm=W.x+W.y+W.z; if(sm>0.0) W/=sm;\n";
    std::string gNrm = useNormal ?
        "    vec3 N1=vmt_smpN(mapNrm,u1),N2=vmt_smpN(mapNrm,u2),N3=vmt_smpN(mapNrm,u3);\n"
        "    N1.y*=" + FY + "; N2.y*=" + FY + "; N3.y*=" + FY + ";\n"
        "    float nc1,ns1,nc2,ns2,nc3,ns3; vmt_rot(v1,rotAmt,nc1,ns1); vmt_rot(v2,rotAmt,nc2,ns2); vmt_rot(v3,rotAmt,nc3,ns3);\n"
        "    N1.xy=vec2(nc1*N1.x-ns1*N1.y, ns1*N1.x+nc1*N1.y);\n"
        "    N2.xy=vec2(nc2*N2.x-ns2*N2.y, ns2*N2.x+nc2*N2.y);\n"
        "    N3.xy=vec2(nc3*N3.x-ns3*N3.y, ns3*N3.x+nc3*N3.y);\n"
        "    vec2 ng1=-N1.xy/max(N1.z,0.001),ng2=-N2.xy/max(N2.z,0.001),ng3=-N3.xy/max(N3.z,0.001);\n"
        "    vec2 nG=W.x*ng1+W.y*ng2+W.z*ng3;\n"
        "    vec3 nrm=normalize(vec3(-nG,1.0))*0.5+0.5;\n"
      : "    vec3 nrm=vec3(0.5,0.5,1.0);\n";

    std::ostringstream gb;
    gb << sn << " " << fn << "(" << gSig.str() << ")\n{\n";
    gb << "    vec2 st = uvCoord*uvScale;\n";
    gb << "    vec3 w; vec2 v1,v2,v3; vmt_tri(st,w,v1,v2,v3);\n";
    gb << "    vec2 u1=vmt_tuv(st,v1,rotAmt), u2=vmt_tuv(st,v2,rotAmt), u3=vmt_tuv(st,v3,rotAmt);\n";
    gb << gW << gNrm;
    gb << "    " << sn << " o;\n";
    for (int i = 0; i < kFixedColorSlots; ++i) {
        if (conn(i)) {
            gb << "    vec3 c" << i << "1=vmt_smp(map" << i << ",u1),c" << i << "2=vmt_smp(map" << i
               << ",u2),c" << i << "3=vmt_smp(map" << i << ",u3);\n";
            if (srgbMask & (1u << i))
                gb << "    c" << i << "1=vmt_s2l(c" << i << "1);c" << i << "2=vmt_s2l(c" << i << "2);c" << i << "3=vmt_s2l(c" << i << "3);\n";
            gb << "    o.outColor" << i << "=W.x*c" << i << "1+W.y*c" << i << "2+W.z*c" << i << "3;\n";
        } else {
            gb << "    o.outColor" << i << "=vec3(0.0);\n";
        }
    }
    gb << "    o.outHeight=" << (useHeight ? "W.x*h1+W.y*h2+W.z*h3" : "0.0") << ";\n";
    gb << "    o.outNormal=nrm;\n    o.outColorNull=vec3(0.0);\n    return o;\n}\n";
    std::string glsl = glslHelpers(useHeight, useNormal) + gb.str();

    // ---- XML ----
    std::ostringstream xml;
    xml << "<fragment uiName=\"" << fn << "\" name=\"" << fn
        << "\" type=\"plumbing\" class=\"ShadeFragment\" version=\"1.0\">\n";
    xml << "  <description><![CDATA[VMT hex tiling (8 fixed slots)]]></description>\n";
    xml << "  <properties>\n";
    xml << "    <float2 name=\"uvCoord\" semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"uvScale\"/>\n    <float name=\"rotAmt\"/>\n";
    for (int i = 0; i < kFixedColorSlots; ++i) if (conn(i)) {
        xml << "    <texture2 name=\"map" << i << "\"/>\n    <sampler name=\"map" << i << "Sampler\"/>\n";
    }
    if (useHeight) {
        xml << "    <float name=\"heightWeight\"/>\n    <float name=\"heightDelta\"/>\n";
        xml << "    <texture2 name=\"mapH\"/>\n    <sampler name=\"mapHSampler\"/>\n";
    }
    if (useNormal) xml << "    <texture2 name=\"mapNrm\"/>\n    <sampler name=\"mapNrmSampler\"/>\n";
    xml << "  </properties>\n";
    xml << "  <outputs>\n    <struct name=\"output\" struct_name=\"" << sn << "\"/>\n  </outputs>\n";
    xml << "  <implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"HLSL\" lang_version=\"11.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n      <source><![CDATA[\n" << hlsl << "\n]]></source>\n    </implementation>\n";
    xml << "    <implementation render=\"OGSRenderer\" language=\"GLSL\" lang_version=\"3.0\">\n";
    xml << "      <function_name val=\"" << fn << "\"/>\n      <source><![CDATA[\n" << glsl << "\n]]></source>\n    </implementation>\n";
    xml << "  </implementation>\n</fragment>\n";
    return MString(xml.str().c_str());
}

MString buildDynGraphXML(const MString& graphName, const MString& implName, unsigned connMask, bool useHeight, int normalMode)
{
    const std::string g = graphName.asChar();
    const std::string im = implName.asChar();
    const std::string sn = dynStructName().asChar();
    const bool useNormal = (normalMode != 0);
    auto conn = [&](int i){ return (connMask & (1u << i)) != 0; };

    std::ostringstream xml;
    xml << "<fragment_graph name=\"" << g << "\" ref=\"" << g << "\" class=\"FragmentGraph\" version=\"1.0\">\n";
    xml << "  <fragments>\n";
    xml << "    <fragment_ref name=\"" << sn << "\" ref=\"" << sn << "\"/>\n";
    xml << "    <fragment_ref name=\"" << im << "\" ref=\"" << im << "\"/>\n";
    xml << "  </fragments>\n";
    xml << "  <connections>\n    <connect from=\"" << im << ".output\" to=\"" << sn << "." << sn << "\" name=\"" << sn << "\"/>\n  </connections>\n";
    xml << "  <properties>\n";
    xml << "    <float2 name=\"uvCoord\" ref=\"" << im << ".uvCoord\" semantic=\"mayaUvCoordSemantic\" flags=\"varyingInputParam\"/>\n";
    xml << "    <float name=\"uvScale\" ref=\"" << im << ".uvScale\"/>\n    <float name=\"rotAmt\" ref=\"" << im << ".rotAmt\"/>\n";
    for (int i = 0; i < kFixedColorSlots; ++i) if (conn(i)) {
        xml << "    <texture2 name=\"map" << i << "\" ref=\"" << im << ".map" << i << "\"/>\n";
        xml << "    <sampler name=\"map" << i << "Sampler\" ref=\"" << im << ".map" << i << "Sampler\"/>\n";
    }
    if (useHeight) {
        xml << "    <float name=\"heightWeight\" ref=\"" << im << ".heightWeight\"/>\n    <float name=\"heightDelta\" ref=\"" << im << ".heightDelta\"/>\n";
        xml << "    <texture2 name=\"mapH\" ref=\"" << im << ".mapH\"/>\n    <sampler name=\"mapHSampler\" ref=\"" << im << ".mapHSampler\"/>\n";
    }
    if (useNormal) {
        xml << "    <texture2 name=\"mapNrm\" ref=\"" << im << ".mapNrm\"/>\n    <sampler name=\"mapNrmSampler\" ref=\"" << im << ".mapNrmSampler\"/>\n";
    }
    xml << "  </properties>\n";
    xml << "  <outputs>\n    <struct name=\"" << sn << "\" ref=\"" << sn << "." << sn << "\"/>\n  </outputs>\n";
    xml << "</fragment_graph>\n";
    return MString(xml.str().c_str());
}

// 指定シグネチャのフラグメント一式を（未登録なら）登録し、グラフ名を返す。
MString registerDynamicFragment(unsigned connMask, unsigned srgbMask, bool useHeight, int normalMode)
{
    MHWRender::MRenderer* renderer = MHWRender::MRenderer::theRenderer();
    if (!renderer) return MString("");
    MHWRender::MFragmentManager* fragMgr = renderer->getFragmentManager();
    if (!fragMgr) return MString("");

    MString gname = dynFragmentName(connMask, srgbMask, useHeight, normalMode);
    if (fragMgr->hasFragment(gname)) return gname; // 既登録

    MString sname = dynStructName();
    MString iname = gname + "_impl";

    if (!fragMgr->hasFragment(sname)) {
        fragMgr->addShadeFragmentFromBuffer(buildDynStructXML().asChar(), false);
        dynRegistry().insert(sname.asChar());
    }
    if (!fragMgr->hasFragment(iname)) {
        fragMgr->addShadeFragmentFromBuffer(buildDynLeafXML(iname, connMask, srgbMask, useHeight, normalMode).asChar(), false);
        dynRegistry().insert(iname.asChar());
    }
    fragMgr->addFragmentGraphFromBuffer(buildDynGraphXML(gname, iname, connMask, useHeight, normalMode).asChar());
    dynRegistry().insert(gname.asChar());
    return gname;
}

// ---------------------------------------------------------------------------
// 事前登録 / 解除（プラグイン init/uninit から呼ぶ。安全なコンテキスト）
// ---------------------------------------------------------------------------
void registerHexTileFragments()
{
    MHWRender::MRenderer* renderer = MHWRender::MRenderer::theRenderer();
    if (!renderer) return; // バッチ/VP2 不在
    MHWRender::MFragmentManager* fragMgr = renderer->getFragmentManager();
    if (!fragMgr) return;

    // 診断フラグメント
    {
        MString vname = hexVizFragmentName();
        if (!fragMgr->hasFragment(vname))
            fragMgr->addShadeFragmentFromBuffer(buildHexVizFragmentXML(vname).asChar(), false);

        // 複数出力 struct 型を宣言する structure フラグメント（全バリアント共有・先に登録）。
        MString sname = vmtHexStructFragmentName();
        if (!fragMgr->hasFragment(sname))
            fragMgr->addShadeFragmentFromBuffer(buildVmtHexStructXML().asChar(), false);

        // 各バリアント: srgb(2)×height(2)×normal(none/OGL/DX=3)。
        // leaf(compute) フラグメント + それを束ねる fragment_graph。
        for (int si = 0; si < 2; ++si)
        for (int hi = 0; hi < 2; ++hi)
        for (int ni = 0; ni < 3; ++ni) {
            bool srgb = (si == 0), useH = (hi == 1);
            MString gname = texCheckFragmentName(srgb, useH, ni);   // 公開（グラフ）名
            MString iname = texCheckImplName(srgb, useH, ni);       // leaf 名
            if (!fragMgr->hasFragment(iname))
                fragMgr->addShadeFragmentFromBuffer(buildTexCheckFragmentXML(iname, srgb, useH, ni).asChar(), false);
            if (!fragMgr->hasFragment(gname))
                fragMgr->addFragmentGraphFromBuffer(buildTexCheckGraphXML(gname, iname, useH, ni).asChar());
        }
    }

    for (int n = 1; n <= kMaxColorMaps; ++n) {
        MString name = hexTileFragmentName(n);
        if (fragMgr->hasFragment(name)) continue;
        MString xml = buildHexTileFragmentXML(n, name);
        fragMgr->addShadeFragmentFromBuffer(xml.asChar(), false);
    }
}

void deregisterHexTileFragments()
{
    MHWRender::MRenderer* renderer = MHWRender::MRenderer::theRenderer();
    if (!renderer) return;
    MHWRender::MFragmentManager* fragMgr = renderer->getFragmentManager();
    if (!fragMgr) return;

    if (fragMgr->hasFragment(hexVizFragmentName()))
        fragMgr->removeFragment(hexVizFragmentName());
    // グラフ → leaf の順で解除し、最後に共有 struct フラグメントを解除。
    for (int si = 0; si < 2; ++si)
    for (int hi = 0; hi < 2; ++hi)
    for (int ni = 0; ni < 3; ++ni) {
        MString gname = texCheckFragmentName(si == 0, hi == 1, ni);
        MString iname = texCheckImplName(si == 0, hi == 1, ni);
        if (fragMgr->hasFragment(gname)) fragMgr->removeFragment(gname);
        if (fragMgr->hasFragment(iname)) fragMgr->removeFragment(iname);
    }
    if (fragMgr->hasFragment(vmtHexStructFragmentName()))
        fragMgr->removeFragment(vmtHexStructFragmentName());

    // 動的生成フラグメント（グラフ→leaf→struct の順で記録逆順に解除）。
    {
        std::set<std::string>& reg = dynRegistry();
        for (auto it = reg.rbegin(); it != reg.rend(); ++it) {
            MString nm(it->c_str());
            if (fragMgr->hasFragment(nm)) fragMgr->removeFragment(nm);
        }
        reg.clear();
    }

    for (int n = 1; n <= kMaxColorMaps; ++n) {
        MString name = hexTileFragmentName(n);
        if (fragMgr->hasFragment(name))
            fragMgr->removeFragment(name);
    }
}

} // namespace vmt
