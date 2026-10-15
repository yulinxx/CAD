cmake_minimum_required(VERSION 4.3)

# ============================================================================
# RenderAbstraction —— 单层结构（纯头文件库 + 独立构建入口同一文件）
# ============================================================================
# 与 Log / FileIO 等双层模块不同，RenderAbstraction 保持单文件兼两角：
#   集成构建：根 CMakeLists.txt → add_subdirectory(RenderAbstraction)（本文件）。
#   独立构建：cmake -S RenderAbstraction -B RenderAbstraction/build。
# 本文件内部自带 project()，两种构建方式走同一份逻辑；状态输出格式与其它
# 模块的独立入口保持一致（见文件末尾摘要方框）。
#
# 本模块是纯头文件 INTERFACE 库（业务级渲染类型与接口，零 RenderX 依赖），
# 没有可执行目标 —— 示例程序随消费方（RenderBridge / UI）提供。
#
# 用法：
#   cmake -S RenderAbstraction -B RenderAbstraction/build
# ============================================================================

project(SanYiRenderAbstraction
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD Rendering Abstraction Layer (header-only interfaces)"
    LANGUAGES CXX
)

# --- 模块元信息 ---
set(LIB_NAME "RenderAbstraction")
set(LIB_FOLDER "Render")
set(LIB_DESCRIPTION "Rendering abstraction layer - business-level types and interfaces")
set(LIB_EXPORT_MACRO "RENDERABSTRACTION_EXPORTS")

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
# 纯头文件库：创建 INTERFACE 目标用于包含路径传播
add_library(${LIB_NAME} INTERFACE)

target_include_directories(${LIB_NAME} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

set_target_properties(${LIB_NAME} PROPERTIES
    FOLDER "Render"
)

sanyi_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    INTERFACE_LINK_LIBRARIES
)

# --- 状态输出 ---------------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Single-layer: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Type:        INTERFACE (header-only)")
message(STATUS "========================================")
message(STATUS "")
