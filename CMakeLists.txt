cmake_minimum_required(VERSION 4.3)

# ============ CMake 策略设置 ============
# CMP0167: FindBoost 模块从 CMake 3.25 开始已被移除，必须使用 CONFIG 模式
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# 包含全局配置文件 (定义所有编译环境选项)
include(Config.cmake)

# 集成VCPKG (使用 Config.cmake 中配置的 VCPKG_DIR)
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# 项目基本信息
project(SanYiCAD
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD System"
    LANGUAGES CXX
)

# 启用 Unity Build（加快编译速度）
option(SANYI_UNITY_BUILD "Enable unity build (faster compilation)" ON)
if(SANYI_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
endif()

set(SANYI_ROOT "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "SanYi CAD project root")
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

# Qt 调试输出控制：非 Debug 构建下禁用 qDebug/qWarning 等调试日志
# 定义 QT_NO_DEBUG_OUTPUT 后，Qt 的调试宏将变为空操作，不输出任何内容
# 注意：此处仅做声明，实际定义在 Main 应用构建时通过生成器表达式动态设置
# 这样可以兼容多配置生成器（Visual Studio, Xcode）

# 注意: C++标准、编译器设置、输出目录等已在 Config.cmake 中配置

# ---------------------------------------------------------------------------
# MSVC 静态分析：补上 printf 格式串校验（默认关闭，CI 专用）
#
# SY_*F 宏（Log/Log/Include/Log/SyLogger.h:194-199）是 C 可变参数，格式串靠标注校验：
#   GCC/Clang —— __attribute__((format(printf,...)))，-Wformat 默认开启即生效；
#   MSVC      —— SAL 的 _Printf_format_string_，**只在 /analyze 下才被检查**。
# 于是「%d 打 qsizetype」这类错误在 Mac/Linux 报警告，在 Windows 一声不响，
# 直到 32 位目标上变成读错栈、后续实参全部错位。本开关把两边拉平。
#
# 为什么不是 /analyze:only —— 它跳过代码生成，不产出 .obj，链接必然失败；
# 要用它得另建构建目录并容忍链接错误，靠翻日志判定结果。/analyze 是
# 「分析 + 正常编译」，能直接复用现有 target 列表，代价是编译变慢（约 2~3x），
# 所以只在独立的 CI 作业里开，本地与发布构建不受影响。
#
# /analyze:external- + /external:* 把 Qt/vcpkg 头排除在分析之外，否则三方头的
# 噪声会把自己代码的告警彻底埋掉。
#
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

# 统一输出目录（缓存变量，跨 project() 作用域可见）
# 注意: 基础目录已在 Config.cmake 中配置，此处添加 Qt 版本后缀以保持兼容性

find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    include(GoogleTest)
    message(STATUS "  Google Test: Found, unit tests enabled")
    
    option(BUILD_ALL_TESTS "Build all unit tests" OFF)
    option(BUILD_UTILITY_TESTS "Build Utility unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_LOG_TESTS "Build Log unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_ENGINE2D_TESTS "Build Engine2D unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_ENGINE3D_TESTS "Build Engine3D unit tests" ${BUILD_ALL_TESTS})
    if(BUILD_NESTING)
        option(BUILD_NESTING_TESTS "Build Nesting unit tests" ${BUILD_ALL_TESTS})
    else()
        option(BUILD_NESTING_TESTS "Build Nesting unit tests" OFF)
    endif()
    if(BUILD_CAM)
        option(BUILD_CAM_TESTS "Build CAM unit tests" ${BUILD_ALL_TESTS})
    else()
        option(BUILD_CAM_TESTS "Build CAM unit tests" OFF)
    endif()
    option(BUILD_GEOMODELCORE_TESTS "Build GeoModelCore unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_FILEIO_TESTS "Build FileIO unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_LICENSE_TESTS "Build License unit tests" ON)
    if(BUILD_ENGRAVING)
        option(BUILD_ENGRAVING_TESTS "Build Engraving unit tests" ON)
    else()
        option(BUILD_ENGRAVING_TESTS "Build Engraving unit tests" OFF)
    endif()
    # 硬件抽象层（设备 HAL / IO 点位 / 安全策略）的测试。
    # 默认 ON：这些测试全部跑在 SimulatedDevice 上，不需要任何真实硬件，
    # 而 HAL 的回归价值恰恰在于「改了适配器不要破坏契约」。
    if(BUILD_HARDWARE)
        option(BUILD_HARDWARE_TESTS "Build Hardware unit tests" ON)
    else()
        option(BUILD_HARDWARE_TESTS "Build Hardware unit tests" OFF)
    endif()
    # 视觉模块测试。默认 ON：全部跑在 SimulatedCamera / FileReplayCamera 上，
    # 不需要真实相机，而视觉的回归价值恰恰在于「改了算子不要破坏契约与精度」。
    if(BUILD_VISION)
        option(BUILD_VISION_TESTS "Build Vision unit tests" ON)
    else()
        option(BUILD_VISION_TESTS "Build Vision unit tests" OFF)
    endif()
    
    enable_testing()
else()
    message(STATUS "  Google Test: Not found, unit tests disabled")
    set(BUILD_UTILITY_TESTS OFF CACHE BOOL "Build Utility unit tests" FORCE)
    set(BUILD_LOG_TESTS OFF CACHE BOOL "Build Log unit tests" FORCE)
    set(BUILD_ENGINE2D_TESTS OFF CACHE BOOL "Build Engine2D unit tests" FORCE)
    set(BUILD_ENGINE3D_TESTS OFF CACHE BOOL "Build Engine3D unit tests" FORCE)
    set(BUILD_GEOMODELCORE_TESTS OFF CACHE BOOL "Build GeoModelCore unit tests" FORCE)
    set(BUILD_NESTING_TESTS OFF CACHE BOOL "Build Nesting unit tests" FORCE)
    set(BUILD_FILEIO_TESTS OFF CACHE BOOL "Build FileIO unit tests" FORCE)
    set(BUILD_LICENSE_TESTS OFF CACHE BOOL "Build License unit tests" FORCE)
    set(BUILD_ENGRAVING_TESTS OFF CACHE BOOL "Build Engraving unit tests" FORCE)
    set(BUILD_HARDWARE_TESTS OFF CACHE BOOL "Build Hardware unit tests" FORCE)
    set(BUILD_VISION_TESTS OFF CACHE BOOL "Build Vision unit tests" FORCE)
endif()

# 输出目录配置 (带 Qt 版本后缀以保持兼容性)
# 注意: 基础目录已在 Config.cmake 中配置
set(_SANYI_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}" CACHE INTERNAL "SanYi runtime output dir")
set(_SANYI_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}" CACHE INTERNAL "SanYi library output dir")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_SANYI_LIB_DIR}")

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_BIN_DIR}/${cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_BIN_DIR}/${cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_LIB_DIR}/${cfg}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

message(STATUS "")
message(STATUS "***************************************************************")

# 1. Core 层 - 共享基础设施
add_subdirectory(Utility/Utility)

# 1.5 CrashHandler 层 - 崩溃捕获（在日志之前，确保初始化阶段崩溃也能捕获）
if(BUILD_CRASHHANDLER)
    add_subdirectory(CrashHandler/CrashHandler)
endif()

# 2. Log 层 - 日志库（Engine 层依赖，License 也依赖）
add_subdirectory(Log/Log)

# 1.6 License 层 - 软件注册与许可校验（依赖 Log）
add_subdirectory(License/License)

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
option(BUILD_RENDERX "Build Renderx rendering engine" ON)
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
if(BUILD_CAM)
    add_subdirectory(CAM/CAM)
    message(STATUS "  CAM: ENABLED")
else()
    message(STATUS "  CAM: DISABLED")
endif()

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
option(BUILD_KEYGEN_TOOL "Build KeygenTool (offline license key generator)" OFF)
if(BUILD_KEYGEN_TOOL)
    add_subdirectory(Tools/KeygenTool)
    message(STATUS "  KeygenTool: ENABLED")
endif()

# 7.6 Metal 原生视口原型（仅 Apple）——一次性可行性验证，不是产品代码。
# 用来回答「Qt 宿主能否把 NSView 交给 Metal」，默认关闭。
option(BUILD_METAL_VIEWPORT_PROTO "Build Metal native viewport prototype (Apple only)" OFF)
if(BUILD_METAL_VIEWPORT_PROTO)
    add_subdirectory(Tools/MetalViewportProto)
endif()

# 8. 应用入口
add_subdirectory(Main)

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

if(BUILD_CAM)
    list(APPEND SANYI_PROJECT_LIBS CAM)
endif()

if(BUILD_UI3D)
    list(APPEND SANYI_PROJECT_LIBS Engine3D UI3D)
endif()

if(BUILD_RENDERX)
    list(APPEND SANYI_PROJECT_LIBS RenderX)
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
# macOS App Bundle: 打包 pdftocairo + Ghostscript 依赖
# ============================================================================
if(APPLE AND NOT CMAKE_CROSSCOMPILING)
    # 查找 Homebrew 安装的工具
    find_program(PDTOCAIRO pdftocairo PATHS /opt/homebrew/bin /usr/local/bin)
    find_program(GHOSTSCRIPT gs PATHS /opt/homebrew/bin /usr/local/bin)
    
    if(PDTOCAIRO AND GHOSTSCRIPT AND TARGET SanYiCAD)
        message(STATUS "Found pdftocairo: ${PDTOCAIRO}")
        message(STATUS "Found ghostscript: ${GHOSTSCRIPT}")
        
        # 打包到 App Bundle (SanYiCAD 是主可执行目标)
        add_custom_command(TARGET SanYiCAD POST_BUILD
            # 创建目录
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/bin
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/lib
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/share/ghostscript
            
            # 复制可执行文件
            COMMAND ${CMAKE_COMMAND} -E copy
                ${PDTOCAIRO}
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/bin/pdftocairo
            COMMAND ${CMAKE_COMMAND} -E copy
                ${GHOSTSCRIPT}
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/bin/gs
            
            # 复制依赖库 (Homebrew 路径下的 dylib)
            COMMAND bash -c "
                cd $<TARGET_FILE_DIR:SanYiCAD>/../Resources/lib &&
                for lib in \$(otool -L ${PDTOCAIRO} 2>/dev/null | grep -E '^\\s+/opt/homebrew|/usr/local' | awk '{print \$1}'); do
                    cp -n \"\$lib\" . 2>/dev/null || true
                done &&
                for lib in \$(otool -L ${GHOSTSCRIPT} 2>/dev/null | grep -E '^\\s+/opt/homebrew|/usr/local' | awk '{print \$1}'); do
                    cp -n \"\$lib\" . 2>/dev/null || true
                done
            "
            
            # 复制 Ghostscript 资源 (字体、初始化文件等)
            COMMAND bash -c "
                GS_ROOT=\$(dirname \$(dirname ${GHOSTSCRIPT}))/share/ghostscript &&
                if [ -d \"\$GS_ROOT\" ]; then
                    cp -R \"\$GS_ROOT\"/* $<TARGET_FILE_DIR:SanYiCAD>/../Resources/share/ghostscript/ 2>/dev/null || true
                fi
            "
            
            # 修正 rpath：让可执行文件在 @loader_path/../lib 找库
            COMMAND install_name_tool -add_rpath @loader_path/../lib
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/bin/pdftocairo
            COMMAND install_name_tool -add_rpath @loader_path/../lib
                $<TARGET_FILE_DIR:SanYiCAD>/../Resources/bin/gs
            
            # 修正库的 install_name 为 @rpath/xxx.dylib
            COMMAND bash -c "
                for f in $<TARGET_FILE_DIR:SanYiCAD>/../Resources/lib/*.dylib; do
                    bn=\$(basename \"\$f\") &&
                    install_name_tool -id @rpath/\$bn \"\$f\" 2>/dev/null || true
                done
            "
            
            COMMENT "Bundling pdftocairo + Ghostscript + deps into App Bundle"
        )
    else()
        message(WARNING "pdftocairo/gs not found - AI/PDF import will not work on other Macs. Install with: brew install poppler ghostscript")
    endif()
endif()

# ============================================================================
# CPack 配置
# ============================================================================
include(CMake/Packaging.cmake)