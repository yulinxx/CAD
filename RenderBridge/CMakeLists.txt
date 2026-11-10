cmake_minimum_required(VERSION 4.3)

# ============================================================================
# RenderBridge —— 单层结构（库本体 + 独立构建入口同一文件）
# ============================================================================
# 与 Log / FileIO 等双层模块不同，RenderBridge 保持单文件兼两角：
#   集成构建：根 CMakeLists.txt → add_subdirectory(RenderBridge)（本文件）。
#   独立构建：cmake -S RenderBridge -B RenderBridge/build（本文件提供 project()）。
# 本文件内部自带 project()，两种构建方式走同一份逻辑；状态输出格式与其它
# 模块的独立入口保持一致（见文件末尾摘要方框）。
#
# 注意：独立构建本文件需要 RenderX / Utility / Log / RenderAbstraction 等目标
# 已存在（通常经 UI 独立工程或根工程拉入），因此不作为日常独立开发入口。
#
# 用法：
#   cmake -S RenderBridge -B RenderBridge/build   # 依赖目标已由上层拉入时
# ============================================================================

project(AprRenderBridge
    VERSION 1.0.0
    DESCRIPTION "April CAD Host-side RenderX session bridge"
    LANGUAGES CXX
)

# --- 模块元信息 ---
set(LIB_NAME "RenderBridge")
set(LIB_FOLDER "UI")
set(LIB_DESCRIPTION "Host-side RenderX session bridge")
set(LIB_EXPORT_MACRO "RENDERBRIDGE_EXPORTS")

# --- 版本（按模块独立）---
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)

# --- 路径 ---
if(NOT APR_ROOT)
    get_filename_component(APR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()
include("${APR_ROOT}/CMake/AprPaths.cmake")
include("${APR_ROOT}/CMake/Utils.cmake")

# --- 源文件收集（自动发现，新增文件无需修改此文件）---
apr_glob_sources(LIB_SOURCES BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Src"
    DIRS
        "."
        "RenderXAdapter"
)

apr_glob_headers(LIB_HEADERS BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    DIRS
        "RenderBridge"
)

# --- 统一入口创建 DLL ---
apr_add_shared_library(${LIB_NAME}
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
    PRIVATE_INCLUDE_DIRS
        # renderx.h 只在 .cpp 内部可见，不传递给下游（UI2D/UI3D 自己按需配置）
        "${APR_RENDERX_DIR}/include"
    # 链接库按 PUBLIC/PRIVATE 分层：
    #   PUBLIC —— 公共头里被引用的依赖，必须传递给下游
    #   PRIVATE —— 仅 .cpp 内部使用，不随 DLL 对外泄漏
    # 此前通过 PUBLIC_INCLUDE_DIRS 硬塞 UICommon/EngineCommon 路径是错误的：
    # 那是把上游工程的头文件路径通过链接闭包泄漏给下游，而不是模块边界。
    # 可见性分离：
    #   PUBLIC_LINK_LIBRARIES —— 公共头里被引用的依赖，必须传递给下游
    #   PRIVATE_LINK_LIBRARIES —— 仅 .cpp 内部使用
    #   PUBLIC_QT_COMPONENTS / PRIVATE_QT_COMPONENTS —— Qt 组件同理
    # 公共头里到处是 Ut::Vec2f/Color/BBox2d/Mat3f（RenderBridgeTypes.h 是唯一类型来源），
    # Utility 必须 PUBLIC 传出去；RenderAbstraction 同理（IRenderScene / IRenderTypes）
    PUBLIC_LINK_LIBRARIES
        Utility
        RenderAbstraction
    PRIVATE_LINK_LIBRARIES
        # 日志只在实现内部打，不随 DLL 对外泄漏
        Log
        # 渲染后端已完全收口到本模块内部：UI 层不再调 rx* API，
        # 因此 RenderX 只以 PRIVATE 链接，不随 RenderBridge 转发给下游
        RenderX
    PRIVATE_QT_COMPONENTS
        # 公共头已中性化，不再 include Qt 类型，全部 Qt 组件收 PRIVATE
        Core Gui OpenGLWidgets
)

apr_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${APR_ROOT}/RenderAbstraction/Include"
        # Utility 是 PUBLIC_LINK_LIBRARIES，通常其 include 由目标自动传递；
        # 显式写出以便脱离根工程独立构建时也能找到 Ut/* 头
        "${APR_ROOT}/Utility/Utility/Include"
    INTERFACE_LINK_LIBRARIES
        Utility::Utility
        Log::Log
        RenderAbstraction
        # RenderX 不在这里：它已是本模块的 PRIVATE 依赖，下游拿不到也不该拿
)

# --- 状态输出 ---------------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Single-layer: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Output:      ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")

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
apr_module_tests_enabled(RENDERBRIDGE _renderbridge_tests)
if(_renderbridge_tests AND GTest_FOUND)
    add_executable(RenderBridgeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/Test/PersistentGeometryStoreTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Test/DrawListBatchMeasurementTests.cpp"
    )
    target_compile_features(RenderBridgeTests PRIVATE cxx_std_17)
    target_include_directories(RenderBridgeTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
        "${APR_RENDERX_DIR}/include"
        "${APR_LOG_DIR}/Include"
    )
    # 被测类的实现已从容头文件移到 RenderBridge 源码里（类型隔离改造），
    # 因此测试需要链 RenderBridge 这个 DLL，不能再只依赖头文件
    target_link_libraries(RenderBridgeTests PRIVATE
        RenderBridge
        RenderX
        Log
        GTest::gtest
        GTest::gtest_main
    )
    # 与 apr_add_library 的 Qt 目标保持同一套 C++17 兼容定义（见 CMake/Utils.cmake）
    target_compile_definitions(RenderBridgeTests PRIVATE QT_NO_P0846)
    if(MSVC)
        target_compile_options(RenderBridgeTests PRIVATE /utf-8)
    endif()
    gtest_discover_tests(RenderBridgeTests
        DISCOVERY_TIMEOUT 30
    )
    set_property(TARGET RenderBridgeTests PROPERTY FOLDER "Tests")
elseif(NOT _renderbridge_tests)
    message(STATUS "[RenderBridge] tests disabled")
endif()
