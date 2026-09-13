cmake_minimum_required(VERSION 4.3)

# --- 模块元信息 ---
set(LIB_NAME "RenderBridge")
set(LIB_FOLDER "UI")
set(LIB_DESCRIPTION "Host-side RenderX session bridge")
set(LIB_EXPORT_MACRO "RENDERBRIDGE_EXPORTS")

# --- 版本（按模块独立）---
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# --- 路径 ---
if(NOT SANYI_ROOT)
    get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

# --- 源文件收集（自动发现，新增文件无需修改此文件）---
sanyi_glob_sources(LIB_SOURCES BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Src"
    DIRS
        "."
)

sanyi_glob_headers(LIB_HEADERS BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    DIRS
        "RenderBridge"
)

# --- 统一入口创建 DLL ---
sanyi_add_shared_library(${LIB_NAME}
    FOLDER "${LIB_FOLDER}"
    DESCRIPTION "${LIB_DESCRIPTION}"
    EXPORT_MACRO "${LIB_EXPORT_MACRO}"
    VERSION_MAJOR ${VERSION_MAJOR}
    VERSION_MINOR ${VERSION_MINOR}
    VERSION_PATCH ${VERSION_PATCH}
    SOURCES ${LIB_SOURCES}
    HEADERS ${LIB_HEADERS}
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${SANYI_RENDERX_DIR}/include"
        "${SANYI_UTILITY_DIR}/Include"
        "${SANYI_LOG_DIR}/Include"
    LINK_LIBRARIES
        PUBLIC
            Utility
            Log
            RenderX
    QT_COMPONENTS
        Core Gui OpenGLWidgets
)

sanyi_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${SANYI_RENDERX_DIR}/include"
    INTERFACE_LINK_LIBRARIES
        Utility::Utility
        Log::Log
        RenderX::RenderX
)

message(STATUS "----------------------------------------")
