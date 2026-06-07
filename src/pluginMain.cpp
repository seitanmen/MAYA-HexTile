// pluginMain.cpp — VMT HexTile PoC plugin entry
// Copyright (c) 2026 kawata / VMT.

#include "VMTHexTilePoc.h"
#include "VMTHexTilePocOverride.h"
#include "FragmentBuilder.h"

#include <maya/MFnPlugin.h>
#include <maya/MDrawRegistry.h>
#include <maya/MGlobal.h>

static const MString kRegistrantId("VMTHexTilePocPlugin");

MStatus initializePlugin(MObject obj)
{
    MStatus st;
    MFnPlugin plugin(obj, "VMT", "0.1.0-poc", "Any", &st);
    if (!st) return st;

    // DG ノード（classification に drawdb を含めて override と紐付け）
    st = plugin.registerNode(
        VMTHexTilePoc::typeName,
        VMTHexTilePoc::id,
        VMTHexTilePoc::creator,
        VMTHexTilePoc::initialize,
        MPxNode::kDependNode,
        &VMTHexTilePoc::classification);
    if (!st) { MGlobal::displayError("registerNode failed: " + st.errorString()); return st; }

    // VP2 シェーディングノードオーバーライド
    st = MHWRender::MDrawRegistry::registerShadingNodeOverrideCreator(
        VMTHexTilePoc::drawClassification,
        kRegistrantId,
        VMTHexTilePocOverride::creator);
    if (!st) { MGlobal::displayError("registerShadingNodeOverrideCreator failed: " + st.errorString()); return st; }

    // フラグメントは安全なこのタイミングで事前登録（override 内では登録しない）
    vmt::registerHexTileFragments();

    MGlobal::displayInfo("VMTHexTilePoc loaded.");
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MStatus st;
    MFnPlugin plugin(obj);

    vmt::deregisterHexTileFragments();

    st = MHWRender::MDrawRegistry::deregisterShadingNodeOverrideCreator(
        VMTHexTilePoc::drawClassification, kRegistrantId);
    if (!st) MGlobal::displayWarning("deregister override failed: " + st.errorString());

    st = plugin.deregisterNode(VMTHexTilePoc::id);
    if (!st) MGlobal::displayWarning("deregisterNode failed: " + st.errorString());

    // 動的生成したフラグメントはここで removeFragment しても良いが、
    // 名前がシグネチャ単位で安定なため PoC では Maya 終了時の破棄に委ねる。
    return MS::kSuccess;
}
