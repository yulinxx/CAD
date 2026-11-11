cmake_minimum_required(VERSION 4.3)

# ============ MSVC 头文件依赖扫描（仅 Ninja 生成器）============
# Ninja 用 rules.ninja 里的 msvc_deps_prefix 逐字节匹配 cl.exe 的 /showIncludes
# 输出，两者编码必须一致，否则一条头文件依赖都记不下来（#deps 0）：改头文件不触发
# 重编 → 新旧 obj 混链 → 报 LNK2005（重复定义）/ LNK2019（找不到符号）。
# 规律（中文 Windows，ANSI=936）：
#   cl 有控制台 → 按控制台代码页输出；无控制台 → 按 ANSI 代码页（936/GBK）输出
#   CMake 写 rules.ninja → 有控制台按控制台代码页转码；无控制台写原始 UTF-8
# 这里把控制台切到 65001，使「有控制台」的 configure 也写出 UTF-8
# 前缀（与无控制台时一致）。
#
# 仅 Ninja 需要这段：Visual Studio/MSBuild 生成器有自己的 tlog 机制做依赖跟踪，
# 不依赖 msvc_deps_prefix 匹配。在 MSBuild 生成器上执行 chcp 是无意义的
# 全局副作用（会污染后续 PowerShell/cmd 会话的控制台代码页），因此这里
# 用 CMAKE_GENERATOR 判定。
if(WIN32 AND CMAKE_GENERATOR MATCHES "Ninja")
    execute_process(COMMAND chcp.com 65001 OUTPUT_QUIET ERROR_QUIET)
endif()

# ============ /showIncludes 手动开关 ============
# 默认关闭：编译日志安静。Ninja 生成器在 chcp 匹配后即使不开 /showIncludes
# 也能正确解析 cl.exe 输出（/showIncludes 由 CMake 自动开启）；
# MSBuild 生成器有自己的 tlog 机制跟踪头文件，完全不依赖 /showIncludes 输出。
#
# 打开用于排查：强制在 cl.exe 命令行加 /showIncludes，把每层 #include 完整刷到
# 编译日志里——但这会产生大量输出，只建议调试依赖问题时临时开启。
option(APR_SHOW_INCLUDES "Enable cl.exe /showIncludes (verbose header inclusion trace)" OFF)

# ============ CMake 策略设置 ============
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

include(Config.cmake)

# 项目基本信息
project(April
    VERSION 1.0.0
    DESCRIPTION "April CAD System"
    # C 语言：Raster 模块有一项"公开头必须是纯 C 头"的编译期契约测试，
    # 需要一个真正的 C 编译器来验证。enable_language() 按 CMake 官方要求
    # 必须在**顶层**目录调用才生效，所以在 project() 里一并启用。
    LANGUAGES CXX C
)

find_package(OpenGL REQUIRED)


set(APR_ROOT "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "April CAD project root")
include("${APR_ROOT}/CMake/AprPaths.cmake")
include("${APR_ROOT}/CMake/Utils.cmake")

# ============ 必需第三方依赖预检（vcpkg）============
# 进入各模块之前一次性检查必需 vcpkg 端口（含纯头文件端口），缺失时汇总输出
# 安装命令并让 configure 明确失败。端口清单维护在 CMake/VcpkgPreflight.cmake。
include("${APR_ROOT}/CMake/VcpkgPreflight.cmake")
apr_check_vcpkg_dependencies()

# 只把格式串相关的分析告警提为错误（其余 /analyze 告警保持告警，避免一次性引入
# 大量非本议题的失败）：
#   C6271 多余实参        C6272 应为浮点   C6273 应为整型   C6274 应为字符
#   C6284 传了对象而非字符串              C6328 类型/宽度不匹配（本次 macOS 报的那类）
#   C6340 有无符号不匹配
# ---------------------------------------------------------------------------
option(APR_MSVC_ANALYZE "Enable MSVC /analyze (SAL printf format-string checking); CI only" OFF)

if(MSVC AND APR_MSVC_ANALYZE)
    if(MSVC_VERSION LESS 1929)
        message(FATAL_ERROR
            "[Analyze] APR_MSVC_ANALYZE requires VS2019 16.10+ (MSVC_VERSION >= 1929) "
            "for /analyze:external-; current MSVC_VERSION=${MSVC_VERSION}")
    endif()

    add_compile_options(
        /analyze
        /analyze:external-
        /external:anglebrackets
        /external:W0
        /we6271 /we6272 /we6273 /we6274 /we6284 /we6328 /we6340
    )
    message(STATUS "  MSVC /analyze: ON (format-string diagnostics promoted to errors)")
endif()

# /showIncludes 开关：两种生成器都支持，但语义不同
#   Ninja：CMake 自动加 /showIncludes 给 cl.exe 做依赖跟踪，本开关仅控制
#          是否把结果额外刷到 stdout（默认不刷）
#   MSBuild：有自己的 tlog 机制跟踪依赖，/showIncludes 仅用于调试
if(MSVC)
    if(APR_SHOW_INCLUDES)
        # add_compile_options(/showIncludes)
        # Ninja 生成器需要 cl.exe 的输出保持 UTF-8，与 rules.ninja 里写的
        # msvc_deps_prefix（UTF-8 编码的中文前缀）一致，否则依赖跟踪断掉；
        # MSBuild 不依赖 msvc_deps_prefix，也不需要这条。
        if(CMAKE_GENERATOR MATCHES "Ninja")
            add_compile_options(/utf-8)
        endif()
        message(STATUS "  cl.exe /showIncludes: ON (verbose header trace enabled)")
    else()
        message(STATUS "  cl.exe /showIncludes: OFF (dependency tracking preserved)")
    endif()
endif()

# ---------------------------------------------------------------------------
# macOS 链接器「ignoring duplicate libraries」告警
#
# vcpkg 静态库经各模块 PUBLIC 链接接口传递（freetype→png/z、protobuf→absl 系、
# SQLiteCpp→sqlite3……），可执行文件的链接行里同一静态库会出现 3~4 次。
# 静态库链接顺序敏感，CMake 不做去重；Apple 链接器（Xcode 15+）对重复项照单
# 全收但只取一份，并刷一条告警。用 -no_warn_duplicate_libraries 关掉这类噪音：
# 不改链接语义。不去动各模块的链接可见性做「根治」——公共头泄漏了
# freetype/protobuf/SQLiteCpp 的 include（TextConverter.h 等），改成 PRIVATE
# 会牵连 include 传递与 Windows 符号导入，收益配不上风险。
# ---------------------------------------------------------------------------
if(APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" APR_LINKER_NO_WARN_DUP_LIBS)
    if(APR_LINKER_NO_WARN_DUP_LIBS)
        add_link_options("LINKER:-no_warn_duplicate_libraries")
        message(STATUS "  链接器 -no_warn_duplicate_libraries: ON（抑制重复静态库告警）")
    endif()
endif()

# ---------------------------------------------------------------------------
# 测试基础设施
# ---------------------------------------------------------------------------
#   1. BUILD_TESTS=ON 时探测 GTest 并接入 GoogleTest；
#   2. GTest 不可用时把唯一总开关 BUILD_TESTS 降级为 OFF（写回缓存并记录原因），
#      降级发生在任何 add_subdirectory 之前，所有模块都只看最终值。
if(BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        enable_testing()
        message(STATUS "  Google Test: Found, unit tests enabled")
    else()
        message(STATUS "  Google Test: Not found, unit tests disabled")
        apr_force_tests_off("GTest not found")
    endif()
endif()

# 输出目录、USE_FOLDERS 等由 Config.cmake 统一配置（唯一配置处）

message(STATUS "")
message(STATUS "***************************************************************")

# 1. Core 层 - 共享基础设施
add_subdirectory(Utility/Utility)

# 1.5 CrashHandler 层 - 崩溃捕获（在日志之前，确保初始化阶段崩溃也能捕获）
if(BUILD_CRASHHANDLER)
    add_subdirectory(CrashHandler/CrashHandler)
endif()

# 2. Log 层 - 日志库
add_subdirectory(Log/Log)

# 1.6 License 层 - 软件注册与许可校验
add_subdirectory(Licensing/License)

# 3. Engine 层 - 引擎核心
add_subdirectory(Engine/Common)
add_subdirectory(Engine/2D)
add_subdirectory(Engine/3D)
add_subdirectory(Engine/Persistence)

# UI → Engine 唯一访问门面（必须在 Engine 目标之后、UI 模块之前定义）
include("${APR_ROOT}/CMake/UiEngineAccess.cmake")
apr_define_ui_engine_access()

# 4.5 渲染抽象层（业务级类型和接口，零RenderX依赖）
add_subdirectory(RenderAbstraction)

# 4. Render 层 - 渲染引擎（Renderx C API）
if(BUILD_RENDERX)
    add_subdirectory(Renderx)
    # RenderBridge：宿主与渲染 DLL 之间的桥接层（会话生命周期、宿主回调）
    add_subdirectory(RenderBridge)
endif()

# 4.6 GeoModelCore（须在 UI3D 之前，以便 UI3D 链接并继承头文件路径）
if(BUILD_GEOMODELCORE)
    find_package(OpenCASCADE CONFIG REQUIRED)
    add_subdirectory(GeoModelCore/GeoModelCore)
    message(STATUS "  GeoModelCore: ENABLED")
else()
    message(STATUS "  GeoModelCore: DISABLED")
endif()

# 4.7 Engraving（须在 UI 之前，以便 UI2D/UI3D 链接）
if(BUILD_ENGRAVING)
    add_subdirectory(Engraving/Engraving)
    message(STATUS "  Engraving: ENABLED")
else()
    message(STATUS "  Engraving: DISABLED")
endif()

# 6. 功能模块（须在 UI 之前：UICommon/UI2D 在 configure 时要求 Nesting 目标存在）
add_subdirectory(FileIO/FileIO)
if(BUILD_NESTING)
    add_subdirectory(Nesting/Nesting)
    message(STATUS "  Nesting: ENABLED")
else()
    message(STATUS "  Nesting: DISABLED")
endif()

# 6.5 CAM 模块 - 激光切割工艺路径生成
# CAM/CAM 已落地，由 BUILD_CAM 开关控制；下面的 6.7 负责实际 add_subdirectory。

# 5. UI 层 - 用户界面
# UICommon 必需；UI2D 全应用必需（Main 有数百处 UI2D 引用且无桩回退）；
# UI3D 可选，由 BUILD_UI3D / BUILD_UI_DIMENSION 控制。
add_subdirectory(UI/Common)

if(NOT BUILD_UI2D)
    message(FATAL_ERROR
        "BUILD_UI2D=OFF is not supported: Main hard-depends on UI2D (no stub fallback). "
        "For a 3D-only build, configure the standalone UI project instead.")
endif()
add_subdirectory(UI/2D)

if(BUILD_UI3D)
    add_subdirectory(UI/3D)
else()
    message(STATUS "  UI3D: DISABLED (BUILD_UI_DIMENSION=${BUILD_UI_DIMENSION})")
endif()

message(STATUS "----------------------------------------")
if(BUILD_HARDWARE)
    add_subdirectory(Hardware/Hardware)
    message(STATUS "  Hardware: ENABLED")
else()
    message(STATUS "  Hardware: DISABLED")
endif()

if(BUILD_NETWORK)
    add_subdirectory(Network/Network)
    message(STATUS "  Network: ENABLED")
else()
    message(STATUS "  Network: DISABLED")
endif()

if(BUILD_VISION)
    add_subdirectory(Vision/Vision)
    message(STATUS "  Vision: ENABLED")
else()
    message(STATUS "  Vision: DISABLED")
endif()

# 6.6 Raster 模块 - 位图处理（OpenCV），面向激光雕刻/切割工艺
# 独立 DLL，公开头只暴露纯 C ABI（POD + 句柄），消费方不需要 OpenCV 头文件。
if(BUILD_RASTER)
    add_subdirectory(Raster/Raster)

    # Raster 是独立仓库的子模块（.gitmodules: git@github.com:yulinxx/Raster.git），
    # 不在本仓维护范围内，因此不直接改它的源码。
    #
    # 其纯 C 契约测试 c_api_compat.c 调用 memset 但未 include <string.h>；
    # C99 起隐式函数声明是错误（-Wimplicit-function-declaration），会中断构建。
    # 这里只对该目标、只在 C 语言下注入头文件，属于构建层修补：
    # 不解禁告警、不降级标准，也不影响任何 C++ 目标。
    # 上游修复后可直接删除本段。
    if(TARGET RasterCApiCompatTest)
        if(MSVC)
            target_compile_options(RasterCApiCompatTest PRIVATE
                $<$<COMPILE_LANGUAGE:C>:/FIstring.h>)
        else()
            target_compile_options(RasterCApiCompatTest PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-include>
                $<$<COMPILE_LANGUAGE:C>:string.h>)
        endif()
    endif()

    message(STATUS "  Raster: ENABLED")
else()
    message(STATUS "  Raster: DISABLED")
endif()

# 6.7 CAM 模块 - 工艺编译层
# 输入 ToolpathJobInput（中性 POD），输出 CamPlan（有序工序 + 路径 + 统计）。
# CAM 不 include Engine2D/UICommon/RenderBridge/Hardware 头；
# Engine2D 类型 → ToolpathJobInput 的转换在 Main/Src/CAM/CamInputBridge（唯一桥接点）。
# 依赖方向：CAM → Log + Utility（PUBLIC），将来 M1 加 Engraving（PRIVATE），M2 加 Hardware/MotionPlan（PRIVATE）。
if(BUILD_CAM)
    add_subdirectory(CAM/CAM)
    message(STATUS "  CAM: ENABLED")
else()
    message(STATUS "  CAM: DISABLED")
endif()

message(STATUS "----------------------------------------")

# 7. Python 集成
# APR_PYTHON_HOST 依赖于 BUILD_PYTHON 开关
if(NOT BUILD_PYTHON)
    set(APR_PYTHON_HOST_DEFAULT OFF)
else()
    set(APR_PYTHON_HOST_DEFAULT ON)
endif()

option(APR_PYTHON_HOST "Enable PythonHost integration framework" ${APR_PYTHON_HOST_DEFAULT})
option(APR_PYTHON_EMBED "Enable in-process Python bridge (Phase 3)" OFF)
set(APR_PYTHON_VERSION "" CACHE STRING "Python version override（留空 = 自动匹配 vcpkg pybind11 自带的版本）")

if(APR_PYTHON_HOST)
    # 必须进内层目录：外层 PythonHost/CMakeLists.txt 是独立构建入口，
    # 其中声明的测试范围只对独立构建生效；作为根工程子目录加入时走内层目录，
    # 避免入口逻辑参与集成构建。
    add_subdirectory(PythonHost/PythonHost)
    message(STATUS "  PythonHost: ENABLED")
else()
    message(STATUS "  PythonHost: DISABLED")
endif()

option(APR_PYBIND_CORE "Build PyBindCore facade extension (_apr_core)" ON)
if(APR_PYBIND_CORE AND APR_PYTHON_HOST)
    # 同样必须进内层目录：外层入口带无参 apr_only_own_tests()（空 = 全部不放行），
    # 那是独立构建语义，不应参与集成构建。
    add_subdirectory(PyBindCore/PyBindCore)
    message(STATUS "  PyBindCore: ENABLED")
else()
    message(STATUS "  PyBindCore: DISABLED")
endif()

# 7.5 KeygenTool - 离线注册码生成工具
if(BUILD_KEYGEN_TOOL)
    add_subdirectory(Tools/KeygenTool)
    message(STATUS "  KeygenTool: ENABLED")
endif()

# 7.55 百万级图元性能基准）
# 建立可重复运行的性能基线，供优化前后对比。
if(BUILD_PERF_BENCHMARK)
    add_subdirectory(Tools/PerfBenchmark)
    message(STATUS "  PerfBenchmark: ENABLED")
endif()

# 7.56 渲染帧路径基准
# 走 RenderX 的 Null 后端，无 GPU / 无窗口依赖，测帧路径的 CPU 侧耗时。
if(BUILD_RENDER_BENCHMARK)
    add_subdirectory(Tools/RenderBenchmark)
    message(STATUS "  RenderBenchmark: ENABLED")
endif()

# 7.6 Metal 原生视口原型（仅 Apple）——一次性可行性验证，不是产品代码。
# 用来回答「Qt 宿主能否把 NSView 交给 Metal」，默认关闭。
option(BUILD_METAL_VIEWPORT_PROTO "Build Metal native viewport prototype (Apple only)" OFF)
if(BUILD_METAL_VIEWPORT_PROTO)
    add_subdirectory(Tools/MetalViewportProto)
endif()

# 8. 应用入口
add_subdirectory(Main)

# 9. 客户 UI 插件（Clients/<clientId> 独立仓库 / submodule）
option(APR_BUILD_CLIENT_PLUGINS "Build per-client UI plugins from Clients/*" ON)
if(APR_BUILD_CLIENT_PLUGINS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Clients/CMakeLists.txt")
    add_subdirectory(Clients)
endif()

# ============================================================================
# 视口后端编译期开关
# ============================================================================
# 只决定「宿主视口用哪个后端」，不改变 Renderx：Metal 后端始终参与编译并与 GL
# 并存（两个 runtime 可在同一进程里各建各的，视口切换时更是常态）。
#
# 关闭此开关 = 根本不编译 Metal 视口代码路径，退回到 OpenGL；这不是运行时回退
# ——Metal 初始化失败属于错误，不做静默降级。
#
# 后端开关统一（2026）：APR_ENABLE_METAL_VIEWPORT 的默认值派生自
# APR_DEFAULT_RENDER_BACKEND（Config.cmake 已在上方 include），不再各自为政，
# 避免「APR_DEFAULT_RENDER_BACKEND=OPENGL 却仍编译 Metal 视口」的错配。
# 用户仍可显式 -DSY_ENABLE_METAL_VIEWPORT=ON/OFF 覆盖默认。
if(APR_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    set(_sy_metal_viewport_default ON)
else()
    set(_sy_metal_viewport_default OFF)
endif()
option(APR_ENABLE_METAL_VIEWPORT
    "Use Metal-backed viewports on Apple platforms (default derived from APR_DEFAULT_RENDER_BACKEND)"
    ${_sy_metal_viewport_default})
unset(_sy_metal_viewport_default)
if(NOT APPLE)
    # 非 Apple 平台没有 Metal 后端，强制关闭并覆盖缓存值，
    # 免得被误设成 ON 后到处出现无法解析的后端分支。
    set(APR_ENABLE_METAL_VIEWPORT OFF CACHE BOOL
        "Use Metal-backed viewports on Apple platforms" FORCE)
endif()
if(APR_ENABLE_METAL_VIEWPORT AND NOT APR_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    message(WARNING
        "[April] APR_ENABLE_METAL_VIEWPORT=ON 但 APR_DEFAULT_RENDER_BACKEND="
        "${APR_DEFAULT_RENDER_BACKEND}；视口将编译 Metal 路径，默认后端却不是 METAL。")
endif()
if(APR_ENABLE_METAL_VIEWPORT)
    message(STATUS "[April] viewport backend: Metal")
else()
    message(STATUS "[April] viewport backend: OpenGL")
endif()

# 编译定义的**唯一**出处：各目标不得自行判断 APPLE 或重复定义开关，
# 否则两处取值不一致时会出现「一半 Metal 一半 GL」的静默错配。
# 未开启时不定义任何宏，视口代码走未定义的 GL 分支。
#
# 必须是 PUBLIC：视口的公共头（RenderWidget.h / RenderWidget3D.h）里这个宏会
# 改变**类定义本身**（基类不同），任何 include 这些头或继承这些类的目标
# （April、MainTests…）都必须拿到同一个取值——否则同一份头在不同 TU 里
# 被解析成不同的类，链接期以 ODR 冲突的形式爆炸。用 PUBLIC 让定义随
# target_link_libraries 自动传播，而不是逐个目标手工补。
set(SY_VIEWPORT_BACKEND_TARGETS UI2D UI3D)
foreach(_sy_viewport_target IN LISTS SY_VIEWPORT_BACKEND_TARGETS)
    if(TARGET ${_sy_viewport_target} AND APR_ENABLE_METAL_VIEWPORT)
        target_compile_definitions(${_sy_viewport_target} PUBLIC APR_ENABLE_METAL_VIEWPORT=1)
    endif()
endforeach()

# ============================================================================
# 依赖方向校验（CI 门禁）— 所有目标已创建后执行
# 入口说明：根工程直接 add_subdirectory(UI/Common|2D|3D)，不会 include
# UI/CMakeLists.txt；那份文件只用于 `cmake -S UI` 独立配置。因此单次
# configure 只执行一次依赖方向校验，不存在重复调用。
# ============================================================================
include("${APR_ROOT}/CMake/CheckDependencies.cmake")
apr_check_dependency_directions()

# ============================================================================
# UI→Engine include 层棘轮校验 — 只冻结存量、禁止增长
# 说明见 CMake/UiEngineAccessRatchet.cmake 文件头。
# ============================================================================
include("${APR_ROOT}/CMake/UiEngineAccessRatchet.cmake")
apr_check_ui_engine_include_ratchet()

# ============================================================================
# 2D 场景写面门禁 — SceneEditService 是唯一写入口
# 说明见 CMake/SceneWriteSurface.cmake 文件头。
# ============================================================================
include("${APR_ROOT}/CMake/SceneWriteSurface.cmake")
apr_check_scene_write_surface()

# ============================================================================
# C ABI 版本查询校验（CI 门禁）— 约定见 Docs/01-当前架构/C-ABI风格约定.md
# ============================================================================
include("${APR_ROOT}/CMake/CheckCApi.cmake")
apr_check_c_api_version()

# ============================================================================
# ENABLE_* 宏传播一致性校验（CI 门禁）
# 影响公共头类型布局的宏必须 PUBLIC 传播,否则不同 DLL/EXE 看到不同 sizeof,
# 跨模块传递对象时 ABI 不一致会堆越界崩溃。
# 校验逻辑:
#   1) 对每个 BUILD_XXX=ON 的开关,验证源头模块已 PUBLIC 传播 ENABLE_XXX
#   2) 同时输出 April 的 INTERFACE_COMPILE_DEFINITIONS(传播结果)供 CI 直观审计
# ============================================================================
message(STATUS "[April] --- ENABLE_* macro propagation check ---")
set(_apr_macro_check_failed FALSE)

# (1) 验证源头模块 PUBLIC 传播(宏传播链的根因检查)
foreach(_apr_pair IN ITEMS
    "BUILD_NESTING:ENABLE_NESTING:UICommon,UI2D,UI3D"
    "BUILD_ENGRAVING:ENABLE_ENGRAVING:UICommon,UI3D"
    "BUILD_HARDWARE:ENABLE_HARDWARE:UI2D"
    "BUILD_GEOMODELCORE:ENABLE_GEOMODELCORE:UI3D"
    "BUILD_VISION:ENABLE_VISION:UI2D"
)
    string(REPLACE ":" ";" _apr_pair_list ${_apr_pair})
    list(GET _apr_pair_list 0 _apr_build_opt)
    list(GET _apr_pair_list 1 _apr_enable_macro)
    list(GET _apr_pair_list 2 _apr_sources_raw)
    # 逗号分隔的模块名列表:转成 CMake 分号列表
    string(REPLACE "," ";" _apr_sources "${_apr_sources_raw}")

    if(${_apr_build_opt})
        foreach(_apr_src IN LISTS _apr_sources)
            if(TARGET ${_apr_src})
                # 检查源头 target 是否 PUBLIC 传播
                get_target_property(_apr_iface_defs ${_apr_src} INTERFACE_COMPILE_DEFINITIONS)
                if(_apr_iface_defs MATCHES "${_apr_enable_macro}")
                    message(STATUS "[April]   OK ${_apr_enable_macro}: ${_apr_src} PUBLIC propagates")
                else()
                    message(WARNING "[April] FAIL ${_apr_enable_macro}: ${_apr_src} NOT PUBLIC propagating!")
                    set(_apr_macro_check_failed TRUE)
                endif()
            endif()
        endforeach()
    else()
        message(STATUS "[April]   · ${_apr_enable_macro}: skipped (BUILD_XXX=OFF)")
    endif()
endforeach()

# (2) BUILD_UI3D 特殊:顶层构建开关,UI 模块不 PUBLIC 传播,Main 源码自己 #if BUILD_UI3D
if(TARGET April)
    if(BUILD_UI3D)
        get_target_property(_apr_private_defs April COMPILE_DEFINITIONS)
        if(_apr_private_defs MATCHES "BUILD_UI3D=1")
            message(STATUS "[April]   OK BUILD_UI3D=1: April PRIVATE (maintained manually, correct)")
        else()
            message(WARNING "[April] FAIL BUILD_UI3D=ON but April does not define BUILD_UI3D=1")
            set(_apr_macro_check_failed TRUE)
        endif()
    endif()
endif()

if(_apr_macro_check_failed)
    message(FATAL_ERROR
        "[April] ENABLE_* macro propagation check failed: ABI consistency risk. "
        "Make sure macros that affect public header type layout propagate through each module's PUBLIC INTERFACE.")
else()
    message(STATUS "[April]   ENABLE_* macro propagation: OK (EXE/DLL consistent)")
endif()
unset(_apr_macro_check_failed)

# ============================================================================
# 安装规则
# ============================================================================
# 安装主可执行文件
install(TARGETS April
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# 安装所有项目动态库
set(APR_PROJECT_LIBS
    Utility Log License
    EngineCommon Engine2D
)
if(BUILD_CRASHHANDLER)
    list(APPEND APR_PROJECT_LIBS CrashHandler)
endif()

list(APPEND APR_PROJECT_LIBS
    UICommon UI2D
    FileIO
    EnginePersistence
    # Engine3D 由 Main 无条件链接（Main/CMakeLists.txt 的 target_link_libraries），
    # 与 BUILD_UI3D 无关。2D-only 安装也必须带上它，否则装出来的 bin/ 缺 DLL 起不来。
    Engine3D
)
if(BUILD_NESTING)
    list(APPEND APR_PROJECT_LIBS Nesting)
endif()

if(BUILD_UI3D)
    list(APPEND APR_PROJECT_LIBS UI3D)
endif()

if(BUILD_RENDERX)
    list(APPEND APR_PROJECT_LIBS RenderX)
    # RenderBridge 是 shared lib 且被 Main 链接（见 Main/CMakeLists.txt）。
    # 此前它只 add_subdirectory 而没进任何 install(TARGETS)，
    # 于是 `cmake --install` 产出的 bin/ 里有 April 与其余 DLL，唯独缺它
    # —— 装出来的应用根本起不来。与 RenderX 同开关，因此一起 append。
    list(APPEND APR_PROJECT_LIBS RenderBridge)
endif()

if(BUILD_HARDWARE)
    list(APPEND APR_PROJECT_LIBS Hardware)
endif()

if(BUILD_ENGRAVING)
    list(APPEND APR_PROJECT_LIBS Engraving)
endif()

if(BUILD_GEOMODELCORE)
    list(APPEND APR_PROJECT_LIBS GeoModelCore)
endif()

if(BUILD_NETWORK)
    list(APPEND APR_PROJECT_LIBS Network)
endif()

if(BUILD_VISION)
    list(APPEND APR_PROJECT_LIBS Vision)
endif()

if(BUILD_RASTER)
    list(APPEND APR_PROJECT_LIBS Raster)
endif()

# CAM 工艺编译层：未安装时装出来的应用缺少 CAM.dll，桥接层与编译入口直接起不来
if(BUILD_CAM)
    list(APPEND APR_PROJECT_LIBS CAM)
endif()

if(APR_PYTHON_HOST)
    list(APPEND APR_PROJECT_LIBS PythonHost)
endif()

# 安装所有项目 DLL/SO 文件
install(TARGETS ${APR_PROJECT_LIBS}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

# 安装 PyBindCore 模块
if(APR_PYBIND_CORE AND APR_PYTHON_HOST)
    install(TARGETS AprPyBindCore
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()

# 安装第三方 DLL（来自 vcpkg）和 Qt 插件
# windeployqt 已将所有内容放置在输出目录中
# 我们只需从输出目录复制所有 .dll、.pyd 文件
if(WIN32)
    install(DIRECTORY "$<TARGET_FILE_DIR:April>/"
        DESTINATION bin
        COMPONENT Runtime
        # 没有 FILES_MATCHING 时，正向 PATTERN 只影响匹配文件的权限，不参与筛选，
        # 结果整个输出目录（含测试 exe、pdb、临时文件）都被装进 bin/。
        FILES_MATCHING
        PATTERN "*.dll"
        PATTERN "*.pyd"
        PATTERN "*.exe" EXCLUDE
        PATTERN "gtest*" EXCLUDE
        PATTERN "gmock*" EXCLUDE
        PATTERN "*Tests.exe" EXCLUDE
        PATTERN "*Benchmark*" EXCLUDE
    )
elseif(UNIX AND NOT APPLE)
    install(DIRECTORY "$<TARGET_FILE_DIR:April>/"
        DESTINATION bin
        COMPONENT Libraries
        FILES_MATCHING
        PATTERN "*.so"
        PATTERN "*.so.*"
    )
endif()

# 安装翻译文件
# macOS 必须放进 bundle 的 Resources/translations，否则发布版永远找不到 .qm
if(APPLE)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/translations"
        DESTINATION "${PROJECT_NAME}.app/Contents/Resources"
        COMPONENT Translations
        OPTIONAL
    )
else()
    install(DIRECTORY "${CMAKE_BINARY_DIR}/translations"
        DESTINATION bin
        COMPONENT Translations
        OPTIONAL
    )
endif()

# 安装资源文件
install(DIRECTORY "${CMAKE_BINARY_DIR}/resources"
    DESTINATION bin
    COMPONENT Resources
    OPTIONAL
)

# 安装 Python 目录
if(APR_PYTHON_HOST)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Python"
        DESTINATION bin
        COMPONENT Python
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
    )
endif()

# ============================================================================
# CPack 配置
# ============================================================================
include(CMake/Packaging.cmake)

# 全部模块与降级动作都已完成后，再打印最终测试状态（唯一打印点）
apr_print_test_status()