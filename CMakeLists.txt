cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(VMTHexTilePoc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 既定: 環境に存在する Maya2026 を使用（-DMAYA_LOCATION で上書き可）
if(NOT MAYA_LOCATION AND NOT DEFINED ENV{MAYA_LOCATION})
    set(MAYA_LOCATION "C:/Program Files/Autodesk/Maya2026")
endif()

find_package(Maya REQUIRED)

add_library(VMTHexTilePoc MODULE
    src/pluginMain.cpp
    src/VMTHexTilePoc.cpp
    src/VMTHexTilePocOverride.cpp
    src/FragmentBuilder.cpp)

target_link_libraries(VMTHexTilePoc PRIVATE Maya::Maya)
target_include_directories(VMTHexTilePoc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
maya_set_plugin_properties(VMTHexTilePoc)

# 出力ファイル名（Maya が旧 .mll をロード中でロックする場合に名前を変えて回避できる）
set(PLUGIN_OUTPUT_NAME "VMTHexTilePoc" CACHE STRING "Output .mll base name")
set_target_properties(VMTHexTilePoc PROPERTIES OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}")

if(MSVC)
    # /utf-8: UTF-8 ソースを正しく読む（C4819 回避） /bigobj: 生成コード対策
    target_compile_options(VMTHexTilePoc PRIVATE /EHsc /MD /utf-8 /bigobj /Zi)
    # /MAP: シンボルマップ /DEBUG: PDB（コード配置は不変）
    target_link_options(VMTHexTilePoc PRIVATE /MAP /DEBUG /OPT:REF /OPT:ICF)
endif()

# 任意デプロイ: -DDEPLOY_DIR=... を渡すとビルド後にコピー
if(DEPLOY_DIR)
    add_custom_command(TARGET VMTHexTilePoc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:VMTHexTilePoc>"
            "${DEPLOY_DIR}/$<TARGET_FILE_NAME:VMTHexTilePoc>"
        COMMENT "Deploying VMTHexTilePoc to ${DEPLOY_DIR}")
endif()

message(STATUS "Configured VMTHexTilePoc (Maya: ${MAYA_LOCATION})")

# ---------------------------------------------------------------------------
# Arnold (MtoA) 翻訳プラグイン（任意。Arnold が見つかれば一緒にビルド）
#   VMTHexTilePoc → OSL ノード vmtHexTile に翻訳して Arnold で描画する拡張 .dll
# ---------------------------------------------------------------------------
if(NOT ARNOLD_LOCATION AND NOT DEFINED ENV{ARNOLD_LOCATION})
    set(ARNOLD_LOCATION "C:/Program Files/Autodesk/Arnold/Maya2026")
endif()

if(EXISTS "${ARNOLD_LOCATION}/include/arnold/ai.h")
    add_library(vmtHexTileArnold MODULE src/arnold/VMTHexTileArnold.cpp)
    target_include_directories(vmtHexTileArnold PRIVATE
        "${ARNOLD_LOCATION}/include/mtoa"
        "${ARNOLD_LOCATION}/include/arnold"
        "${ARNOLD_LOCATION}/include")
    target_link_libraries(vmtHexTileArnold PRIVATE Maya::Maya
        "${ARNOLD_LOCATION}/lib/ai.lib"
        "${ARNOLD_LOCATION}/lib/mtoa_api.lib")
    # MtoA 拡張は .dll（接頭辞 lib なし）
    set_target_properties(vmtHexTileArnold PROPERTIES PREFIX "" SUFFIX ".dll")
    if(MSVC)
        target_compile_options(vmtHexTileArnold PRIVATE /EHsc /MD /utf-8 /bigobj /Zi)
        target_link_options(vmtHexTileArnold PRIVATE /DEBUG)
    endif()
    if(DEPLOY_ARNOLD_DIR)
        add_custom_command(TARGET vmtHexTileArnold POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:vmtHexTileArnold>"
                "${DEPLOY_ARNOLD_DIR}/$<TARGET_FILE_NAME:vmtHexTileArnold>"
            COMMENT "Deploying vmtHexTileArnold to ${DEPLOY_ARNOLD_DIR}")
    endif()
    message(STATUS "Configured vmtHexTileArnold (Arnold: ${ARNOLD_LOCATION})")
else()
    message(STATUS "Arnold not found at ${ARNOLD_LOCATION}; skipping vmtHexTileArnold")
endif()
