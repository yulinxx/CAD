# ============================================================================
# Config.cmake - April CAD 全局构建配置（手动配置）
# ============================================================================

if(APR_CONFIG_INCLUDED)
    return()
endif()
set(APR_CONFIG_INCLUDED TRUE)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [1] 路径配置
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
set(QT_VERSION_MAJOR    6                       CACHE STRING "Qt 主版本" FORCE)
set(Qt_INSTALL_DIR      "$ENV{HOME}/Qt/6.12.0/macos"  CACHE PATH "Qt6 安装路径" FORCE)
set(VCPKG_DIR           "$ENV{HOME}/vcpkg"      CACHE PATH "vcpkg 根目录" FORCE)

if(VCPKG_DIR STREQUAL "" OR NOT EXISTS "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "[Config] VCPKG_DIR not found: '${VCPKG_DIR}' — 请在 Config.cmake 修改")
endif()
if(Qt_INSTALL_DIR STREQUAL "" OR NOT EXISTS "${Qt_INSTALL_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    if(NOT APR_ALLOW_MISSING_QT)
        message(FATAL_ERROR
            "[Config] Qt_INSTALL_DIR not found: '${Qt_INSTALL_DIR}' — 请在 Config.cmake 修改")
    endif()
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")
set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/CMake/vcpkg-overlays"
    CACHE STRING "vcpkg overlay ports (repo-local)")

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [2] 构建基础
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_UNITY_BUILD ON)

# 输出目录
set(_APR_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}")
set(_APR_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}")
foreach(_apr_type RUNTIME LIBRARY ARCHIVE)
    set(CMAKE_${_apr_type}_OUTPUT_DIRECTORY "${_APR_BIN_DIR}" CACHE PATH "" FORCE)
endforeach()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_APR_LIB_DIR}" CACHE PATH "" FORCE)
foreach(_apr_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_apr_cfg}" _apr_cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_apr_cfg_u}  "${_APR_BIN_DIR}/${_apr_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_apr_cfg_u}  "${_APR_BIN_DIR}/${_apr_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_apr_cfg_u} "${_APR_LIB_DIR}/${_apr_cfg}")
endforeach()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Qt
set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake config dir" FORCE)
list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")
if(WIN32 AND EXISTS "${Qt_INSTALL_DIR}")
    set(ENV{PATH} "${Qt_INSTALL_DIR}/bin;${VCPKG_DIR}/installed/x64-windows/bin;$ENV{PATH}")
    set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "" FORCE)
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [3] 渲染 & UI 配置
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(APPLE)
    set(APR_DEFAULT_RENDER_BACKEND "METAL"  CACHE STRING "Render backend: METAL|OPENGL" FORCE)
else()
    set(APR_DEFAULT_RENDER_BACKEND "OPENGL" CACHE STRING "Render backend: METAL|OPENGL" FORCE)
endif()

set(BUILD_UI_DIMENSION "BOTH" CACHE STRING "UI 维度: 2D|3D|BOTH" FORCE)
set_property(CACHE BUILD_UI_DIMENSION PROPERTY STRINGS 2D 3D BOTH)
if(BUILD_UI_DIMENSION STREQUAL "2D")
    set(BUILD_UI3D OFF  CACHE BOOL "Build UI3D" FORCE)
else()
    set(BUILD_UI3D ON   CACHE BOOL "Build UI3D" FORCE)
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [4] 模块开关（手动配置，FORCE 每次覆盖）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# 核心模块
option(BUILD_RENDERX    "Renderx 渲染引擎"          ON)
option(BUILD_UI2D       "UI2D 界面（必需）"          ON)
option(BUILD_NESTING    "套料算法"                    ON)

# 可选模块
option(BUILD_VISION     "视觉模块"                    OFF)
option(BUILD_CAM        "CAM 加工层"                  ON)
option(BUILD_RASTER     "Raster 位图处理"             ON)
option(BUILD_NETWORK    "网络模块"                    OFF)
option(BUILD_HARDWARE   "硬件控制"                    OFF)
option(BUILD_ENGRAVING  "3D 激光雕刻"                 OFF)
option(BUILD_GEOMODELCORE "OpenCASCADE 几何建模"      OFF)
# OpenCASCADE 最低版本：GeoModelCore 源码已切到 OCCT 8.0+ API
# （Standard_Failure::what() / 直用 NCollection_HSequence<TopoDS_Shape> /
# ConnectEdgesToWires() 返回值重载等），低于此版本会在编译期给出晦涩错误。
# 这里集中定义下限，由 CMake/VcpkgPreflight.cmake（全局）与
# GeoModelCore/CMakeLists.txt（模块本地）双重门禁在 configure 阶段明确提示。
set(APR_OCCT_MIN_VERSION "8.0.0")
option(BUILD_CRASHHANDLER "崩溃捕获"                  OFF)
option(BUILD_PYTHON     "PythonHost 集成"             OFF)

# 工具 & 开关
option(BUILD_KEYGEN_TOOL   "离线授权工具"             OFF)
option(BUILD_PERF_BENCHMARK "性能基准测试"             ON)
option(BUILD_RENDER_BENCHMARK "渲染帧路径基准"         ON)
option(APR_ENABLE_LICENSE  "启用许可证校验"           ON)
option(BUILD_TESTS         "构建单元测试"              ON)

# 子模块缺失保护：用户显式 ON 但目录不存在时明确报错
foreach(_apr_pair
    "Renderx:Renderx"
    "Nesting:Nesting/Nesting"
    "Vision:Vision/Vision"
    "CAM:CAM/CAM"
    "Raster:Raster/Raster"
    "Network:Network/Network"
    "Hardware:Hardware/Hardware"
    "Engraving:Engraving/Engraving"
    "GeoModelCore:GeoModelCore/GeoModelCore"
    "CrashHandler:CrashHandler/CrashHandler"
    "Python:PythonHost/PythonHost")
    string(REPLACE ":" ";" _apr_list "${_apr_pair}")
    list(GET _apr_list 0 _apr_name)
    list(GET _apr_list 1 _apr_dir)
    if(BUILD_${_apr_name} AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_apr_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "BUILD_${_apr_name}=ON 但子模块 ${_apr_dir}/ 未检出\n"
            "  git submodule update --init ${_apr_dir}")
    endif()
endforeach()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [5] 渲染后端校验
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(APR_DEFAULT_RENDER_BACKEND STREQUAL "METAL" AND NOT APPLE)
    message(FATAL_ERROR "Metal 后端仅 macOS 支持")
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [6] 测试基础设施（被各模块 CMakeLists.txt include）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
set(APR_TEST_MODULES
    UTILITY LOG ENGINECOMMON ENGINE2D ENGINE3D
    GEOMODELCORE FILEIO LICENSE NESTING ENGRAVING
    HARDWARE VISION RASTER NETWORK PYTHONHOST
    CRASHHANDLER RENDERX RENDERBRIDGE
    UI_COMMON UI2D UI3D MAIN VIEWPORT_REFRESH SCENE_EDIT CAM
    CACHE INTERNAL "Test module keys")

macro(apr_module_tests_enabled _module _out_var)
    set(${_out_var} OFF)
    if(BUILD_TESTS)
        if(NOT DEFINED APR_STANDALONE_OWN_MODULES
           OR "${_module}" IN_LIST APR_STANDALONE_OWN_MODULES)
            set(${_out_var} ON)
        endif()
    endif()
endmacro()

macro(apr_force_tests_off _reason)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF)
    set(APR_TESTS_DISABLED_REASON "${_reason}")
    message(WARNING "[Tests] BUILD_TESTS forced OFF: ${_reason}")
endmacro()

function(apr_print_test_status)
    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        return()
    endif()
    message(STATUS "[Tests] BUILD_TESTS: ${BUILD_TESTS}")
    if(NOT BUILD_TESTS AND APR_TESTS_DISABLED_REASON)
        message(STATUS "  reason: ${APR_TESTS_DISABLED_REASON}")
    endif()
endfunction()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  [7] 配置汇总
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
message(STATUS "======================================================================")
message(STATUS "[Config] Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "[Config] Qt:           ${Qt_INSTALL_DIR}")
message(STATUS "[Config] vcpkg:         ${VCPKG_DIR}")
message(STATUS "[Config] Render:       ${APR_DEFAULT_RENDER_BACKEND}")
message(STATUS "[Config] UI Dimension:  ${BUILD_UI_DIMENSION}")
message(STATUS "======================================================================")
