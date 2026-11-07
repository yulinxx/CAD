# ============================================================================
# Config.cmake - SanYi CAD 全局构建配置
# ============================================================================
#
# 使用方法：
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# ============================================================================
#                         📌 常用配置（在此修改）
# ============================================================================

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [1] Qt6 路径（根据操作系统自动选择）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(WIN32)
    # set(Qt_INSTALL_DIR "C:/Qt/6.11.0/msvc2022_64/" CACHE PATH "Qt6 Windows")
    set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.12.0/msvc2022_64/" CACHE PATH "Qt6 Windows")
elseif(APPLE)
    set(Qt_INSTALL_DIR "$ENV{HOME}/Qt/6.11.1/macos/" CACHE PATH "Qt6 macOS")
elseif(UNIX)
    set(Qt_INSTALL_DIR "$ENV{HOME}/Install/Qt/6.11.1/gcc_64/" CACHE PATH "Qt6 Linux")
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [2] vcpkg 路径（根据操作系统自动选择）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(WIN32)
    # set(VCPKG_DIR "C:/vcpkg/" CACHE PATH "vcpkg Windows")
    set(VCPKG_DIR "C:/Users/xx/vcpkg/" CACHE PATH "vcpkg Windows")
elseif(APPLE)
    set(VCPKG_DIR "$ENV{HOME}/vcpkg/" CACHE PATH "vcpkg macOS")
elseif(UNIX)
    set(VCPKG_DIR "$ENV{HOME}/Install/vcpkg/" CACHE PATH "vcpkg Linux")
endif()

# 构建类型: Debug | Release | RelWithDebInfo | MinSizeRel
#
# 只在「调用方没有指定」时给默认值。**不能用裸 set()**：那会创建一个普通变量，
# 遮蔽 cache 里由 -DCMAKE_BUILD_TYPE= 或 CMakePresets 写入的值，且本文件每次
# configure 都会重新执行一遍。实测后果是 CMakeCache.txt 里记着 Debug，
# 实际编译却按 Release（-O3 -DNDEBUG）产出——全仓 assert() 因此被编译掉，
# 其中包含 ParallelRegion 构造/析构配对的唯一检查。
#
# 多配置生成器（VS/Xcode）不使用 CMAKE_BUILD_TYPE，此处置默认值对它们无副作用；
# 这里不能查 CMAKE_CONFIGURATION_TYPES，因为它由 project() 设置，而本文件在
# project() 之前被 include。
if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Build type: Debug | Release | RelWithDebInfo | MinSizeRel")
endif()

# ============================================================================
#                         📌 模块开关（按需启用）
#
# 模块开关的唯一声明处在下方「核心功能模块」一节（约 200 行处）。
# 之前这里还有一份重复的 option() 列表，且 BUILD_GEOMODELCORE 两处默认值相反
# （此处 ON、下方 OFF）—— option() 对已存在的缓存项是空操作，于是「默认开还是关」
# 取决于构建目录的历史，干净机器上会默认开启 GeoModelCore 并要求 OpenCASCADE。
# 重复声明已删除，模块开关只保留一份。
# ============================================================================

# 测试总开关：全工程唯一开关。
# ON  = 按各模块是否具备测试源码正常构建（GTest 缺失时自动降级为 OFF）
# OFF = 任何模块都不生成测试目标
option(BUILD_TESTS "构建全部模块测试" ON)

# ============================================================================

# 本文件是路径配置与测试开关的**唯一配置处**
# 重复 include 保护
if(SANYI_CONFIG_INCLUDED)
    return()
endif()
set(SANYI_CONFIG_INCLUDED TRUE)

# Qt 主版本号
set(QT_VERSION_MAJOR 6)

# 路径未命中时明确失败，避免后续 configure 产生难懂的连锁错误
if(VCPKG_DIR STREQUAL "" OR NOT EXISTS "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "[Config] VCPKG_DIR not found or invalid: '${VCPKG_DIR}'\n"
        "  Set environment variable VCPKG_DIR, or pass -DVCPKG_DIR=<path> to cmake.")
endif()
if(Qt_INSTALL_DIR STREQUAL "" OR NOT EXISTS "${Qt_INSTALL_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    # 纯算法模块（如 Nesting）不链接 Qt：它的独立入口会在 include 本文件之前设
    # SANYI_ALLOW_MISSING_QT=ON，从而在没有 Qt 的机器/CI 上也能配置。
    # 默认关闭该旁路，其他模块的报错与行为保持原样。
    if(NOT SANYI_ALLOW_MISSING_QT)
        message(FATAL_ERROR
            "[Config] Qt_INSTALL_DIR not found or invalid: '${Qt_INSTALL_DIR}'\n"
            "  Set environment variable Qt_INSTALL_DIR, or pass -DQt_INSTALL_DIR=<path> to cmake.")
    endif()
endif()

# --------------------------------------------------------------------
# vcpkg 工具链
# --------------------------------------------------------------------
# 必须在 project() 之前生效 —— 根脚本在 project() 之前 include 本文件，正是为此。
# 各模块不得再自行 set(CMAKE_TOOLCHAIN_FILE ...)。
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

# --------------------------------------------------------------------
# 构建类型：唯一默认值定义在上面的「构建类型」小节（见 set(CMAKE_BUILD_TYPE ...)）。
# 这里只做状态输出。原先此处还有一份重复的 `if(NOT CMAKE_BUILD_TYPE)` 兜底，
# 与上面那份是同一个配置的两个来源 —— 已删除，保持单一事实源。
message(STATUS "======================================================================")
message(STATUS "[Config] Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "======================================================================")

# --------------------------------------------------------------------
# C++ 标准配置
# --------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------
# 符号可见性控制
# --------------------------------------------------------------------
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# --------------------------------------------------------------------
# Unity Build 配置 (可加速编译)
# --------------------------------------------------------------------
option(SANYI_UNITY_BUILD "Enable unity build (faster compilation)" ON)
if(SANYI_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
endif()

# --------------------------------------------------------------------
# 输出目录配置
# --------------------------------------------------------------------
# 统一带 Qt 版本后缀，避免同机多 Qt 版本的产物互相覆盖。
# 这是本项目输出目录的唯一配置处：各模块不得再自行 set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ...)。
set(_SANYI_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}")
set(_SANYI_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}" CACHE PATH "Runtime output directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}" CACHE PATH "Library output directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_SANYI_LIB_DIR}" CACHE PATH "Archive output directory")

# 多配置生成器（Visual Studio / Xcode）需要再按配置分子目录
foreach(_sanyi_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_sanyi_cfg}" _sanyi_cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_BIN_DIR}/${_sanyi_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_BIN_DIR}/${_sanyi_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_LIB_DIR}/${_sanyi_cfg}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --------------------------------------------------------------------
# Qt 配置
# --------------------------------------------------------------------
set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake configuration directory")
list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")

# Windows: 配置 PATH 以包含 Qt 和 vcpkg 二进制目录
if(WIN32 AND EXISTS "${Qt_INSTALL_DIR}")
    set(_SANYI_QT_BIN "${Qt_INSTALL_DIR}/bin")
    set(_SANYI_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
    set(ENV{PATH} "${_SANYI_QT_BIN};${_SANYI_VCPKG_BIN};$ENV{PATH}")
    set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "Qt plugins directory")
endif()

# ===== 渲染后端配置 =====
set(SANYI_RENDER_BACKENDS "OPENGL;METAL;VULKAN" CACHE STRING "Available render backends")
if(APPLE)
    set(SANYI_DEFAULT_RENDER_BACKEND "METAL" CACHE STRING "Default render backend: OPENGL|METAL|VULKAN")
else()
    set(SANYI_DEFAULT_RENDER_BACKEND "OPENGL" CACHE STRING "Default render backend: OPENGL|METAL|VULKAN")
endif()

# ===== UI 维度配置 =====
set(SANYI_UI_DIMENSIONS "2D;3D" CACHE STRING "Available UI dimensions")
set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension: 2D|3D")

# ===== 可选模块开关：默认值按「子模块是否已检出」自适应 =====
# 干净 clone 里没跑 `git submodule update --init` 时子模块目录是空的，
# 而这些 BUILD_xxx 原本一律默认 ON，根 CMakeLists 随即
# `add_subdirectory(<空目录>)` → configure 直接失败；连根本不需要该模块的
# 2D-only 构建也被拖垮。约定：
#   - 目录已检出 → 默认 ON（保持原行为）
#   - 目录缺失   → 默认 OFF，并打印原因
#   - 用户显式 -DBUILD_XXX=ON 但目录缺失 → 明确报错并给出初始化命令，
#     不静默改写用户意图，也不让它落到 CMake 那句看不懂的报错上。
# option() 不覆盖命令行 / cache 里已有的值，显式设置始终被尊重。
# 注意：本文件在顶层 project() 之前被 include，用 CMAKE_CURRENT_LIST_DIR
# 捕获源码根，别依赖 PROJECT_SOURCE_DIR。
# ============================================================================
set(SANYI_CONFIG_ROOT "${CMAKE_CURRENT_LIST_DIR}")
function(sanyi_module_option _name _dir _doc)
    set(_cmakelists "${SANYI_CONFIG_ROOT}/${_dir}/CMakeLists.txt")
    if(EXISTS "${_cmakelists}")
        option(${_name} "${_doc}" ON)
    else()
        option(${_name} "${_doc}" OFF)
        message(STATUS "[SanYi] ${_name}: 默认 OFF（子模块 ${_dir}/ 未检出）")
    endif()
    if(${_name} AND NOT EXISTS "${_cmakelists}")
        message(FATAL_ERROR
            "[SanYi] ${_name}=ON 但子模块 ${_dir}/ 未检出（缺 ${_cmakelists}）。\n"
            "  请先执行: git submodule update --init ${_dir}\n"
            "  或改用:   -D${_name}=OFF")
    endif()
endfunction()

# ===== 核心功能模块 =====
# 建议保持默认开启状态
sanyi_module_option(BUILD_RENDERX "Renderx" "Build Renderx rendering engine (3D rendering core)")
option(BUILD_UI2D "Build UI2D module (2D user interface, always ON)" ON)

# ---- UI 维度门控（唯一权威开关）----
# 用法：cmake -DBUILD_UI_DIMENSION=2D
#   BOTH（默认）：UI2D + UI3D 都构建（保持历史行为）
#   2D          ：关闭 UI3D，只产出 2D 目标（Main 自动走 UI3D 桩头文件回退）
#   3D          ：构建 UI3D，并把运行时默认视口设为 3D
# 注意：UI2D 是全应用必需（Main 有数百处 UI2D 引用），任何取值都不会关闭它；
#       因此 "3D" 表示"启用 3D 且默认 3D"，而非"仅 3D"。
# BUILD_UI3D / SANYI_DEFAULT_UI_DIMENSION 均由本开关派生（FORCE 写回缓存，
# 避免 2D→BOTH 切换时缓存粘滞）。
set(BUILD_UI_DIMENSION "BOTH" CACHE STRING "UI build dimension gate: 2D|3D|BOTH")
set_property(CACHE BUILD_UI_DIMENSION PROPERTY STRINGS 2D 3D BOTH)
if(BUILD_UI_DIMENSION STREQUAL "2D")
    set(BUILD_UI3D OFF CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension (derived)" FORCE)
elseif(BUILD_UI_DIMENSION STREQUAL "3D")
    set(BUILD_UI3D ON CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "3D" CACHE STRING "Default UI dimension (derived)" FORCE)
elseif(BUILD_UI_DIMENSION STREQUAL "BOTH")
    set(BUILD_UI3D ON CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension (derived)" FORCE)
else()
    message(FATAL_ERROR
        "[SanYi] BUILD_UI_DIMENSION 取值非法: '${BUILD_UI_DIMENSION}'（应为 2D|3D|BOTH）")
endif()

sanyi_module_option(BUILD_NESTING "Nesting/Nesting" "Build Nesting module (2D/3D nesting/arrangement algorithm)")

# ===== 可选功能模块 =====
# 按需启用（默认值见上方 sanyi_module_option：子模块未检出时自动 OFF）
sanyi_module_option(BUILD_VISION "Vision/Vision" "Build Vision module (image processing, computer vision)")
sanyi_module_option(BUILD_CAM "CAM/CAM" "Build CAM layer (toolpath planning, process compilation)")
sanyi_module_option(BUILD_RASTER "Raster/Raster" "Build Raster module (OpenCV bitmap processing for laser engraving/cutting)")
sanyi_module_option(BUILD_NETWORK "Network/Network" "Build Network module (HTTP, WebSocket, cloud sync)")
sanyi_module_option(BUILD_HARDWARE "Hardware/Hardware" "Build Hardware module (laser control, material database)")
sanyi_module_option(BUILD_ENGRAVING "Engraving/Engraving" "Build Engraving module (3D laser engraving)")
sanyi_module_option(BUILD_GEOMODELCORE "GeoModelCore/GeoModelCore" "Build GeoModelCore module (OpenCASCADE-based geometry modeling)")
sanyi_module_option(BUILD_CRASHHANDLER "CrashHandler/CrashHandler" "Build CrashHandler module (crash capture and reporting)")
sanyi_module_option(BUILD_PYTHON "PythonHost/PythonHost" "Build Python module (PythonHost integration framework)")
# 许可校验开关：默认 ON；开发构建如需关闭，用 -DSANYI_ENABLE_LICENSE=OFF。
# 宏编进 License 库本身（LicenseInternal.cpp 依据它决定运行期默认值），
# Main 的 BuildConfig.h 镜像同步该取值供启动自检交叉核对。
option(SANYI_ENABLE_LICENSE "启用许可校验（发货构建应设 ON）" ON)

# ===== 工具 =====
option(BUILD_KEYGEN_TOOL "Build KeygenTool (offline license key generator)" OFF)
# 百万级图元性能基准（Tools/PerfBenchmark）。默认构建，便于随时复测与优化前后对比；
# 本项按 CMake 常规：首次 configure 写入缓存，需要关闭时用 -DBUILD_PERF_BENCHMARK=OFF。
option(BUILD_PERF_BENCHMARK "Build million-entity performance benchmark" ON)
# 渲染帧路径基准（Tools/RenderBenchmark）。走 RenderX 的 Null 后端，
# 不依赖 GPU 与窗口系统，可无人值守复现，默认构建。
option(BUILD_RENDER_BENCHMARK "Build headless render frame-path benchmark" ON)

# --------------------------------------------------------------------
# 测试开关（本项目测试开关的唯一配置处）
# --------------------------------------------------------------------
# 唯一总开关 BUILD_TESTS 在本文件开头「测试开关」一节声明，默认 ON。
# 旧的 BUILD_ALL_TESTS 与逐模块 BUILD_*_TESTS 已全部废弃，任何模块都不得再
# 定义同名开关。是否生成测试目标只看 BUILD_TESTS 与当前构建范围
# （集成构建 = 全部模块；独立构建 = sanyi_only_own_tests 声明的模块键）。

# 已废弃的旧开关清单：只用于迁移告警，不参与任何新判断。
set(SANYI_LEGACY_TEST_SWITCHES
    BUILD_ALL_TESTS
    BUILD_UTILITY_TESTS
    BUILD_LOG_TESTS
    BUILD_ENGINECOMMON_TESTS
    BUILD_ENGINE2D_TESTS
    BUILD_ENGINE3D_TESTS
    BUILD_GEOMODELCORE_TESTS
    BUILD_FILEIO_TESTS
    BUILD_LICENSE_TESTS
    BUILD_NESTING_TESTS
    BUILD_ENGRAVING_TESTS
    BUILD_HARDWARE_TESTS
    BUILD_VISION_TESTS
    BUILD_RASTER_TESTS
    BUILD_NETWORK_TESTS
    BUILD_PYTHONHOST_TESTS
    BUILD_CRASHHANDLER_TESTS
    BUILD_RENDERX_TESTS
    BUILD_RENDERBRIDGE_TESTS
    BUILD_UI_COMMON_TESTS
    BUILD_UI2D_TESTS
    BUILD_UI3D_TESTS
    BUILD_MAIN_TESTS
    BUILD_VIEWPORT_REFRESH_TESTS
    BUILD_SCENE_EDIT_TESTS
    CACHE INTERNAL "已废弃的旧测试开关清单")

# 模块键清单：独立构建裁剪与状态输出使用。键名必须与内层
# sanyi_module_tests_enabled(<键>) 的调用一致。
set(SANYI_TEST_MODULES
    UTILITY
    LOG
    ENGINECOMMON
    ENGINE2D
    ENGINE3D
    GEOMODELCORE
    FILEIO
    LICENSE
    NESTING
    ENGRAVING
    HARDWARE
    VISION
    RASTER
    NETWORK
    PYTHONHOST
    CRASHHANDLER
    RENDERX
    RENDERBRIDGE
    UI_COMMON
    UI2D
    UI3D
    MAIN
    VIEWPORT_REFRESH
    SCENE_EDIT
    CAM
    CACHE INTERNAL "测试模块键清单")

# 旧开关迁移告警：缓存里残留旧变量时明确提示，旧值不得影响新行为。
# 每次 configure 都检查，直到用户删除缓存项或换全新构建目录。
set(_sanyi_legacy_found "")
foreach(_sanyi_legacy IN LISTS SANYI_LEGACY_TEST_SWITCHES)
    if(DEFINED ${_sanyi_legacy})
        list(APPEND _sanyi_legacy_found "${_sanyi_legacy}=${${_sanyi_legacy}}")
    endif()
endforeach()
if(_sanyi_legacy_found)
    message(WARNING
        "[Tests] 检测到已废弃的测试开关：${_sanyi_legacy_found}\n"
        "  BUILD_ALL_TESTS 与 BUILD_*_TESTS 已废弃并被忽略，当前只认 BUILD_TESTS。\n"
        "  请删除对应缓存项（cmake -U <变量名> ...）或使用全新构建目录重新 configure。")
endif()
unset(_sanyi_legacy_found)
unset(_sanyi_legacy)

# 判定单个模块是否构建测试：BUILD_TESTS 与独立构建允许范围同时成立。
# 用法：sanyi_module_tests_enabled(VISION _vision_tests)
# SANYI_STANDALONE_OWN_MODULES 未定义 = 集成构建，默认全部模块放行；
# 已定义（含空字符串）= 独立构建，只有列表中的模块键放行。
macro(sanyi_module_tests_enabled _module _out_var)
    set(${_out_var} OFF)
    if(BUILD_TESTS)
        if(NOT DEFINED SANYI_STANDALONE_OWN_MODULES)
            set(${_out_var} ON)
        elseif("${_module}" IN_LIST SANYI_STANDALONE_OWN_MODULES)
            set(${_out_var} ON)
        endif()
    endif()
endmacro()

# 测试不可用时统一降级：写回缓存并记录原因，供状态输出使用。
# 必须同时更新当前作用域的普通变量：option() 会创建同名普通变量，
# CMP0126 NEW 下 set(... CACHE) 不会清除它，只改缓存会让 if(BUILD_TESTS) 仍看到 ON。
macro(sanyi_force_tests_off _reason)
    set(BUILD_TESTS OFF CACHE BOOL "BUILD_TESTS forced OFF: ${_reason}" FORCE)
    set(BUILD_TESTS OFF)
    set(SANYI_TESTS_DISABLED_REASON "${_reason}")
endmacro()

# 测试状态输出：必须在全部降级动作之后调用，展示最终生效值。
# 只在顶层工程打印，避免集成构建里被各模块重复调用。
function(sanyi_print_test_status)
    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        return()
    endif()
    message(STATUS "[Tests]")
    if(BUILD_TESTS)
        message(STATUS "  BUILD_TESTS: ON (final)")
    else()
        set(_reason "${SANYI_TESTS_DISABLED_REASON}")
        if(NOT _reason)
            set(_reason "disabled by user")
        endif()
        message(STATUS "  BUILD_TESTS: OFF (final, reason: ${_reason})")
    endif()
    if(DEFINED SANYI_STANDALONE_OWN_MODULES)
        if(SANYI_STANDALONE_OWN_MODULES)
            message(STATUS "  Scope: standalone (${SANYI_STANDALONE_OWN_MODULES})")
        else()
            message(STATUS "  Scope: standalone (none)")
        endif()
    else()
        message(STATUS "  Scope: all modules")
    endif()
endfunction()

# --------------------------------------------------------------------
# 依赖库查找配置
# --------------------------------------------------------------------

# Vulkan 支持开关。
# 必须在这里（读取它的分支之前）声明：option() 只在首次读取时生效，
# 声明在使用之后会让「首次 configure」与「二次 configure」行为不同 ——
# 干净构建目录里变量未定义，VULKAN 分支会误报「已禁用」。
option(SANYI_ENABLE_VULKAN "Enable Vulkan rendering support (not implemented yet)" ON)

# 渲染后端选择逻辑（OpenGL 查找已延迟至 CMakeLists.txt project() 之后）
if(SANYI_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    if(APPLE)
        # Metal 是 macOS 系统框架，不需要 find_package
        message(STATUS "[Render] Backend: Metal (Apple)")
    else()
        message(FATAL_ERROR "[Render] Metal backend only supported on macOS")
    endif()
elseif(SANYI_DEFAULT_RENDER_BACKEND STREQUAL "VULKAN")
    # 渲染层当前只有 GL / Metal / Null 三套 RHI 实现（Renderx/src/rhi），
    # 没有 Vulkan 后端。开关保留是为了让「选 Vulkan」得到一个明确的失败，
    # 而不是配到一半才在运行时找不到后端。
    message(FATAL_ERROR
        "[Render] Vulkan backend is not implemented yet (available: OPENGL, METAL). "
        "Renderx/src/rhi has gl/ metal/ null only.")
else()
    # OpenGL：不在此处 find_package，改而在 CMakeLists.txt project() 之后调用
    # 参见 CMakeLists.txt 中在 project() 后的 openGL 初始化
    if(APPLE)
        message(STATUS "[Render] Backend: Metal (fallback from OpenGL on macOS)")
    else()
        message(STATUS "[Render] Backend: OpenGL (will be found after project())")
    endif()
endif()

# OpenCASCADE 配置 (GeoModelCore 需要)
if(BUILD_GEOMODELCORE)
    # OpenCASCADE 通常通过 vcpkg 安装
    # 如需指定自定义路径，取消注释下面一行:
    # set(OpenCASCADE_DIR "${VCPKG_DIR}/installed/x64-windows/share/opencascade")
endif()

# --------------------------------------------------------------------
# 最终配置汇总
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "[Qt]")
message(STATUS "  Install Directory: ${Qt_INSTALL_DIR}")
message(STATUS "  Config Directory: ${Qt6_DIR}")
message(STATUS "[vcpkg]")
message(STATUS "  VCPKG_DIR: ${VCPKG_DIR}")
message(STATUS "[Render Backend]")
message(STATUS "  Default: ${SANYI_DEFAULT_RENDER_BACKEND}")
message(STATUS "[UI Dimension]")
message(STATUS "  Default: ${SANYI_DEFAULT_UI_DIMENSION}")
message(STATUS "[Modules]")
message(STATUS "  RenderX:        ${BUILD_RENDERX}")
message(STATUS "  UI2D:           ${BUILD_UI2D}")
message(STATUS "  UI3D:           ${BUILD_UI3D}")
message(STATUS "  Vision:         ${BUILD_VISION}")
message(STATUS "  Raster:         ${BUILD_RASTER}")
message(STATUS "  Network:        ${BUILD_NETWORK}")
message(STATUS "  Hardware:       ${BUILD_HARDWARE}")
message(STATUS "  Engraving:      ${BUILD_ENGRAVING}")
message(STATUS "  Nesting:        ${BUILD_NESTING}")
message(STATUS "  GeoModelCore:   ${BUILD_GEOMODELCORE}")
message(STATUS "  CrashHandler:   ${BUILD_CRASHHANDLER}")
message(STATUS "  Python:         ${BUILD_PYTHON}")
# [Tests] 状态块已移除：include 时刻的取值不是最终值（GTest 降级发生在
# 根 CMakeLists.txt / sanyi_find_gtest 之后）。最终状态统一由
# sanyi_print_test_status() 在降级完成后打印。
message(STATUS "")
