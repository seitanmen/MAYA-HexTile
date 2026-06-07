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
