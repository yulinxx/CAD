cmake_minimum_required(VERSION 4.3)

# ============ MSVC 头文件依赖扫描（Ninja）============
# ninja 用 rules.ninja 里的 msvc_deps_prefix 逐字节匹配 cl.exe 的 /showIncludes
# 输出，两者编码必须一致，否则一条头文件依赖都记不下来（#deps 0）：改头文件不触发
# 重编 → 新旧 obj 混链 → 报 LNK2005（重复定义）/ LNK2019（找不到符号）。
# 规律（中文 Windows，ANSI=936）：
#   cl 有控制台 → 按控制台代码页输出；无控制台 → 按 ANSI 代码页（936/GBK）输出
#   CMake 写 rules.ninja → 有控制台按控制台代码页转码；无控制台写原始 UTF-8
# 这里在 project() 之前把控制台切到 65001，使「有控制台」的 configure 也写出 UTF-8
# 前缀（与无控制台时一致），并要求构建侧同样使用 UTF-8 控制台（VS Code 内置终端
# 默认即 65001；老式 cmd/PowerShell 里先执行 chcp 65001）。
# 症状自查：构建日志大段刷「注意: 包含文件:」即依赖匹配失败。
if(WIN32)
    execute_process(COMMAND chcp.com 65001 OUTPUT_QUIET ERROR_QUIET)
endif()

# ============ CMake 策略设置 ============
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

include(Config.cmake)

# 项目基本信息
project(SanYiCAD
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD System"
    # C 语言：Raster 模块有一项"公开头必须是纯 C 头"的编译期契约测试，
    # 需要一个真正的 C 编译器来验证。enable_language() 按 CMake 官方要求
    # 必须在**顶层**目录调用才生效，所以在 project() 里一并启用。
    LANGUAGES CXX C
)

find_package(OpenGL REQUIRED)


set(SANYI_ROOT "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "SanYi CAD project root")
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

# 只把格式串相关的分析告警提为错误（其余 /analyze 告警保持告警，避免一次性引入
# 大量非本议题的失败）：
#   C6271 多余实参        C6272 应为浮点   C6273 应为整型   C6274 应为字符
#   C6284 传了对象而非字符串              C6328 类型/宽度不匹配（本次 macOS 报的那类）
#   C6340 有无符号不匹配
# ---------------------------------------------------------------------------
option(SANYI_MSVC_ANALYZE "Enable MSVC /analyze (SAL printf format-string checking); CI only" OFF)

if(MSVC AND SANYI_MSVC_ANALYZE)
    if(MSVC_VERSION LESS 1929)
        message(FATAL_ERROR
            "[Analyze] SANYI_MSVC_ANALYZE requires VS2019 16.10+ (MSVC_VERSION >= 1929) "
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
    check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries" SANYI_LINKER_NO_WARN_DUP_LIBS)
    if(SANYI_LINKER_NO_WARN_DUP_LIBS)
        add_link_options("LINKER:-no_warn_duplicate_libraries")
        message(STATUS "  链接器 -no_warn_duplicate_libraries: ON（抑制重复静态库告警）")
    endif()
endif()

# ---------------------------------------------------------------------------
# 测试基础设施
# ---------------------------------------------------------------------------
#   1. 探测 GTest 并接入 GoogleTest；
#   2. GTest 不可用时，把 Config.cmake 里的全部测试开关统一降级为 OFF。
find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    include(GoogleTest)
    enable_testing()
    message(STATUS "  Google Test: Found, unit tests enabled")
else()
    message(STATUS "  Google Test: Not found, unit tests disabled")
    foreach(_sanyi_test IN LISTS SANYI_TEST_SWITCHES)
        set(${_sanyi_test} OFF CACHE BOOL "disabled: GTest not found" FORCE)
    endforeach()
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
include("${SANYI_ROOT}/CMake/UiEngineAccess.cmake")
sanyi_define_ui_engine_access()

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
# 仓库里暂时没有 CAM 模块目录，因此不再提供 BUILD_CAM 开关：
# 开关在但目录不在，打开就是 configure 直接失败（add_subdirectory 指向不存在的路径）。
# CAM 模块落地后，把下面这段与 Config.cmake 里的 option 一起加回。

# 5. UI 层 - 用户界面
# UICommon 必需；UI2D 全应用必需（Main 有数百处 UI2D 引用且无桩回退）；
# UI3D 可选，由 BUILD_UI3D / BUILD_UI_DIMENSION 控制。
add_subdirectory(UI/Common)

if(NOT BUILD_UI2D)
    message(FATAL_ERROR
        "BUILD_UI2D=OFF 不受支持：Main 硬依赖 UI2D（无桩回退）。"
        "如需仅 3D，请构建 UI 独立工程。")
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
    message(STATUS "  Raster: ENABLED")
else()
    message(STATUS "  Raster: DISABLED")
endif()

message(STATUS "----------------------------------------")

# 7. Python 集成
# SANYI_PYTHON_HOST 依赖于 BUILD_PYTHON 开关
if(NOT BUILD_PYTHON)
    set(SANYI_PYTHON_HOST_DEFAULT OFF)
else()
    set(SANYI_PYTHON_HOST_DEFAULT ON)
endif()
option(SANYI_PYTHON_HOST "Enable PythonHost integration framework" ${SANYI_PYTHON_HOST_DEFAULT})
option(SANYI_PYTHON_EMBED "Enable in-process Python bridge (Phase 3)" OFF)
set(SANYI_PYTHON_VERSION "3.11" CACHE STRING "Required Python version")

if(SANYI_PYTHON_HOST)
    add_subdirectory(PythonHost)
    message(STATUS "  PythonHost: ENABLED")
else()
    message(STATUS "  PythonHost: DISABLED")
endif()

option(SANYI_PYBIND_CORE "Build PyBindCore facade extension (_sanyi_core)" ON)
if(SANYI_PYBIND_CORE AND SANYI_PYTHON_HOST)
    add_subdirectory(PyBindCore)
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
option(SANYI_BUILD_CLIENT_PLUGINS "Build per-client UI plugins from Clients/*" ON)
if(SANYI_BUILD_CLIENT_PLUGINS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Clients/CMakeLists.txt")
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
# 后端开关统一（2026）：SY_ENABLE_METAL_VIEWPORT 的默认值派生自
# SANYI_DEFAULT_RENDER_BACKEND（Config.cmake 已在上方 include），不再各自为政，
# 避免「SANYI_DEFAULT_RENDER_BACKEND=OPENGL 却仍编译 Metal 视口」的错配。
# 用户仍可显式 -DSY_ENABLE_METAL_VIEWPORT=ON/OFF 覆盖默认。
if(SANYI_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    set(_sy_metal_viewport_default ON)
else()
    set(_sy_metal_viewport_default OFF)
endif()
option(SY_ENABLE_METAL_VIEWPORT
    "Use Metal-backed viewports on Apple platforms (default derived from SANYI_DEFAULT_RENDER_BACKEND)"
    ${_sy_metal_viewport_default})
unset(_sy_metal_viewport_default)
if(NOT APPLE)
    # 非 Apple 平台没有 Metal 后端，强制关闭并覆盖缓存值，
    # 免得被误设成 ON 后到处出现无法解析的后端分支。
    set(SY_ENABLE_METAL_VIEWPORT OFF CACHE BOOL
        "Use Metal-backed viewports on Apple platforms" FORCE)
endif()
if(SY_ENABLE_METAL_VIEWPORT AND NOT SANYI_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    message(WARNING
        "[SanYi] SY_ENABLE_METAL_VIEWPORT=ON 但 SANYI_DEFAULT_RENDER_BACKEND="
        "${SANYI_DEFAULT_RENDER_BACKEND}；视口将编译 Metal 路径，默认后端却不是 METAL。")
endif()
if(SY_ENABLE_METAL_VIEWPORT)
    message(STATUS "[SanYi] viewport backend: Metal")
else()
    message(STATUS "[SanYi] viewport backend: OpenGL")
endif()

# 编译定义的**唯一**出处：各目标不得自行判断 APPLE 或重复定义开关，
# 否则两处取值不一致时会出现「一半 Metal 一半 GL」的静默错配。
# 未开启时不定义任何宏，视口代码走未定义的 GL 分支。
#
# 必须是 PUBLIC：视口的公共头（RenderWidget.h / RenderWidget3D.h）里这个宏会
# 改变**类定义本身**（基类不同），任何 include 这些头或继承这些类的目标
# （SanYiCAD、MainTests…）都必须拿到同一个取值——否则同一份头在不同 TU 里
# 被解析成不同的类，链接期以 ODR 冲突的形式爆炸。用 PUBLIC 让定义随
# target_link_libraries 自动传播，而不是逐个目标手工补。
set(SY_VIEWPORT_BACKEND_TARGETS UI2D UI3D)
foreach(_sy_viewport_target IN LISTS SY_VIEWPORT_BACKEND_TARGETS)
    if(TARGET ${_sy_viewport_target} AND SY_ENABLE_METAL_VIEWPORT)
        target_compile_definitions(${_sy_viewport_target} PUBLIC SY_ENABLE_METAL_VIEWPORT=1)
    endif()
endforeach()

# ============================================================================
# 依赖方向校验（CI 门禁）— 所有目标已创建后执行
# ============================================================================
include("${SANYI_ROOT}/CMake/CheckDependencies.cmake")
sanyi_check_dependency_directions()

# ============================================================================
# C ABI 版本查询校验（CI 门禁）— 约定见 Docs/01-当前架构/C-ABI风格约定.md
# ============================================================================
include("${SANYI_ROOT}/CMake/CheckCApi.cmake")
sanyi_check_c_api_version()

# ============================================================================
# 安装规则
# ============================================================================
# 安装主可执行文件
install(TARGETS SanYiCAD
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# 安装所有项目动态库
set(SANYI_PROJECT_LIBS
    Utility Log License
    EngineCommon Engine2D
)
if(BUILD_CRASHHANDLER)
    list(APPEND SANYI_PROJECT_LIBS CrashHandler)
endif()

list(APPEND SANYI_PROJECT_LIBS
    UICommon UI2D
    FileIO
    EnginePersistence
)
if(BUILD_NESTING)
    list(APPEND SANYI_PROJECT_LIBS Nesting)
endif()

if(BUILD_UI3D)
    list(APPEND SANYI_PROJECT_LIBS Engine3D UI3D)
endif()

if(BUILD_RENDERX)
    list(APPEND SANYI_PROJECT_LIBS RenderX)
    # RenderBridge 是 shared lib 且被 Main 链接（见 Main/CMakeLists.txt）。
    # 此前它只 add_subdirectory 而没进任何 install(TARGETS)，
    # 于是 `cmake --install` 产出的 bin/ 里有 SanYiCAD 与其余 DLL，唯独缺它
    # —— 装出来的应用根本起不来。与 RenderX 同开关，因此一起 append。
    list(APPEND SANYI_PROJECT_LIBS RenderBridge)
endif()

if(BUILD_HARDWARE)
    list(APPEND SANYI_PROJECT_LIBS Hardware)
endif()

if(BUILD_ENGRAVING)
    list(APPEND SANYI_PROJECT_LIBS Engraving)
endif()

if(BUILD_GEOMODELCORE)
    list(APPEND SANYI_PROJECT_LIBS GeoModelCore)
endif()

if(BUILD_NETWORK)
    list(APPEND SANYI_PROJECT_LIBS Network)
endif()

if(BUILD_VISION)
    list(APPEND SANYI_PROJECT_LIBS Vision)
endif()

if(BUILD_RASTER)
    list(APPEND SANYI_PROJECT_LIBS Raster)
endif()

if(SANYI_PYTHON_HOST)
    list(APPEND SANYI_PROJECT_LIBS PythonHost)
endif()

# 安装所有项目 DLL/SO 文件
install(TARGETS ${SANYI_PROJECT_LIBS}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

# 安装 PyBindCore 模块
if(SANYI_PYBIND_CORE AND SANYI_PYTHON_HOST)
    install(TARGETS SanYiPyBindCore
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()

# 安装第三方 DLL（来自 vcpkg）和 Qt 插件
# windeployqt 已将所有内容放置在输出目录中
# 我们只需从输出目录复制所有 .dll、.pyd 文件
if(WIN32)
    install(DIRECTORY "$<TARGET_FILE_DIR:SanYiCAD>/"
        DESTINATION bin
        COMPONENT Runtime
        PATTERN "*.dll"
        PATTERN "*.pyd"
        PATTERN "*.exe" EXCLUDE
        PATTERN "gtest*" EXCLUDE
        PATTERN "gmock*" EXCLUDE
        PATTERN "*Tests.exe" EXCLUDE
        PATTERN "*Benchmark*" EXCLUDE
    )
elseif(UNIX AND NOT APPLE)
    install(DIRECTORY "$<TARGET_FILE_DIR:SanYiCAD>/"
        DESTINATION bin
        COMPONENT Libraries
        PATTERN "*.so"
        PATTERN "*.so.*"
    )
endif()

# 安装翻译文件
install(DIRECTORY "${CMAKE_BINARY_DIR}/translations"
    DESTINATION bin
    COMPONENT Translations
    OPTIONAL
)

# 安装资源文件
install(DIRECTORY "${CMAKE_BINARY_DIR}/resources"
    DESTINATION bin
    COMPONENT Resources
    OPTIONAL
)

# 安装 Python 目录
if(SANYI_PYTHON_HOST)
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