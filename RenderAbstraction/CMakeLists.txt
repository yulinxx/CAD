cmake_minimum_required(VERSION 4.3)

# --- 模块元信息 ---
set(LIB_NAME "RenderAbstraction")
set(LIB_FOLDER "Render")
set(LIB_DESCRIPTION "Rendering abstraction layer - business-level types and interfaces")
set(LIB_EXPORT_MACRO "RENDERABSTRACTION_EXPORTS")

# --- 版本 ---
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

# --- 源文件收集 ---
sanyi_glob_headers(LIB_HEADERS BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    DIRS
        "RenderAbstraction"
)

# RenderAbstraction 是纯头文件库（仅接口定义）
# 无需编译源文件，类型定义和接口声明都在 Include 中

# --- 统一入口创建库 ---
# 纯头文件库：创建一个 OBJECT 目标用于包含路径传播
add_library(${LIB_NAME} INTERFACE)

target_include_directories(${LIB_NAME} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

set_target_properties(${LIB_NAME} PROPERTIES
    FOLDER "Render"
    PROJECT_VERSION "${PROJECT_VERSION}"
)

sanyi_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    INTERFACE_LINK_LIBRARIES
)

message(STATUS "[${LIB_NAME}]")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Description: ${LIB_DESCRIPTION}")
message(STATUS "----------------------------------------")
