# MayaConfig.cmake — Locate the Maya SDK (headers + libs) from an install root.
#
# Usage:
#   set(MAYA_LOCATION "C:/Program Files/Autodesk/Maya2026")  # or env MAYA_LOCATION
#   find_package(Maya REQUIRED)
#   target_link_libraries(myPlugin PRIVATE Maya::Maya)
#   maya_set_plugin_properties(myPlugin)   # .mll/.so/.bundle 拡張子と前置詞

# ---------- resolve MAYA_LOCATION -------------------------------------------
if(NOT MAYA_LOCATION)
    if(DEFINED ENV{MAYA_LOCATION})
        set(MAYA_LOCATION "$ENV{MAYA_LOCATION}")
    endif()
endif()

if(NOT MAYA_LOCATION)
    if(WIN32)
        file(GLOB _maya_dirs "C:/Program Files/Autodesk/Maya20[0-9][0-9]")
    elseif(APPLE)
        file(GLOB _maya_dirs "/Applications/Autodesk/maya20[0-9][0-9]")
    else()
        file(GLOB _maya_dirs "/usr/autodesk/maya20[0-9][0-9]*")
    endif()
    if(_maya_dirs)
        list(SORT _maya_dirs COMPARE NATURAL ORDER DESCENDING)
        list(GET _maya_dirs 0 MAYA_LOCATION)
        message(STATUS "Auto-detected Maya at: ${MAYA_LOCATION}")
    endif()
    unset(_maya_dirs)
endif()

if(NOT MAYA_LOCATION)
    message(FATAL_ERROR "Maya not found. Set -DMAYA_LOCATION=C:/Program Files/Autodesk/Maya2026")
endif()

# ---------- include / lib ----------------------------------------------------
set(MAYA_INCLUDE_DIR "${MAYA_LOCATION}/include")
if(NOT EXISTS "${MAYA_INCLUDE_DIR}/maya/MPxNode.h")
    message(FATAL_ERROR "Maya headers not found under ${MAYA_INCLUDE_DIR}/maya/")
endif()

if(WIN32)
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/lib")
elseif(APPLE)
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/Maya.app/Contents/MacOS")
else()
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/lib")
endif()

set(_maya_libs Foundation OpenMaya OpenMayaUI OpenMayaAnim OpenMayaRender OpenMayaFX)
set(_maya_resolved "")
foreach(_l ${_maya_libs})
    find_library(MAYA_${_l}_LIB NAMES ${_l} PATHS "${MAYA_LIB_DIR}" NO_DEFAULT_PATH)
    if(MAYA_${_l}_LIB)
        list(APPEND _maya_resolved "${MAYA_${_l}_LIB}")
    endif()
endforeach()

if(NOT MAYA_OpenMaya_LIB)
    message(FATAL_ERROR "OpenMaya library not found in ${MAYA_LIB_DIR}")
endif()

# ---------- imported target --------------------------------------------------
if(NOT TARGET Maya::Maya)
    add_library(Maya::Maya INTERFACE IMPORTED)
    set_target_properties(Maya::Maya PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MAYA_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${_maya_resolved}")
    if(WIN32)
        set_property(TARGET Maya::Maya APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS "NT_PLUGIN;_USE_MATH_DEFINES")
    elseif(APPLE)
        set_property(TARGET Maya::Maya APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS "OSMac_;REQUIRE_IOSTREAM")
    else()
        set_property(TARGET Maya::Maya APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS "LINUX;REQUIRE_IOSTREAM;_BOOL")
    endif()
endif()

# ---------- helper: plugin file naming --------------------------------------
function(maya_set_plugin_properties TARGET)
    set_target_properties(${TARGET} PROPERTIES PREFIX "")
    if(WIN32)
        set_target_properties(${TARGET} PROPERTIES SUFFIX ".mll")
    elseif(APPLE)
        set_target_properties(${TARGET} PROPERTIES SUFFIX ".bundle")
    else()
        set_target_properties(${TARGET} PROPERTIES SUFFIX ".so")
    endif()
endfunction()

message(STATUS "Maya SDK found: ${MAYA_LOCATION}")
