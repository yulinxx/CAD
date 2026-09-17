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
        # 覆盖层与屏幕定尺寸标记几何共用应用层绘制词汇（Render::Vec2f / Color / BBox2d）
        "${SANYI_UI_COMMON_DIR}/Include"
        # RenderTypes.h 里的离散化常量来自 EngineCommon（Eg::tess）
        "${SANYI_ENGINE_COMMON_DIR}/Include"
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
        "${SANYI_UI_COMMON_DIR}/Include"
        "${SANYI_ENGINE_COMMON_DIR}/Include"
    INTERFACE_LINK_LIBRARIES
        Utility::Utility
        Log::Log
        RenderX::RenderX
)

message(STATUS "----------------------------------------")

# ============================================================================
# 宿主侧几何仓契约测试（独立目标）
# ============================================================================
# RenderBridge 此前没有任何测试目标，而 PersistentGeometryStore 是 2D/3D 两个
# builder 共用的块管理底座，现在还承担着「用多个仓绕过 DLL 单仓 4GB 上限」的分片
# 逻辑 —— 路由错了的表现是「某些图元不画」，很难从画面定位。
#
# 用 Null 后端 Runtime：几何仓的分配/写入/释放只需要 Runtime，不需要 GPU，
# 因此这些用例在没有显卡的机器上也能跑（与 Renderx 自己的 RxRuntimeTests 同一套用法）。
#
# 用独立 option 名（而不是复用 BUILD_*_TESTS）：Config.cmake 用普通变量统一
# set(... OFF) 会遮蔽同名 option，这是仓库里既有的坑（见 Main 的 ViewportRefreshTests）。
option(BUILD_RENDERBRIDGE_TESTS "Build RenderBridge host-side contract tests" ON)
if(BUILD_RENDERBRIDGE_TESTS AND GTest_FOUND)
    add_executable(RenderBridgeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/Test/PersistentGeometryStoreTests.cpp"
    )
    target_compile_features(RenderBridgeTests PRIVATE cxx_std_17)
    target_include_directories(RenderBridgeTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${SANYI_RENDERX_DIR}/include"
        "${SANYI_LOG_DIR}/Include"
    )
    # 被测类与头文件里的包装都是 header-only，因此不需要链 RenderBridge 这个 DLL
    target_link_libraries(RenderBridgeTests PRIVATE
        RenderX
        Log
        GTest::gtest
        GTest::gtest_main
    )
    if(MSVC)
        target_compile_options(RenderBridgeTests PRIVATE /utf-8)
    endif()
    gtest_discover_tests(RenderBridgeTests
        DISCOVERY_TIMEOUT 30
    )
    set_property(TARGET RenderBridgeTests PROPERTY FOLDER "Tests")
elseif(NOT BUILD_RENDERBRIDGE_TESTS)
    message(STATUS "[RenderBridge] tests disabled")
endif()
