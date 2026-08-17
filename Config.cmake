# ============================================================================
# Config.cmake — 公共构建配置（入库）
# ============================================================================
# 此文件包含所有可入库的公共构建逻辑。
# 用户本地私有配置请使用 Config.local.cmake（已被 .gitignore 排除）。
# ============================================================================

# --------------------------------------------------------------------
# 加载用户本地配置（可选）
# --------------------------------------------------------------------
# [A1 修复] 支持 Config.local.cmake 覆盖默认路径，实现"公共配置入库 + 私有配置不入库"
# 注意：使用绝对路径，因为子模块（如 Renderx）会通过 ../Config.cmake 直接 include 此文件。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/Config.local.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/Config.local.cmake")
    message(STATUS "[Config] Loaded local configuration from Config.local.cmake")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/Config.local.cmake")
    include("${CMAKE_SOURCE_DIR}/Config.local.cmake")
    message(STATUS "[Config] Loaded local configuration from Config.local.cmake")
endif()

# --------------------------------------------------------------------
# vcpkg 配置（默认值，可通过 Config.local.cmake 或 -DVCPKG_DIR= 覆盖）
# --------------------------------------------------------------------
if(NOT VCPKG_DIR)
    if(WIN32)
        set(VCPKG_DIR "C:/Users/xx/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(UNIX AND NOT APPLE)
        set(VCPKG_DIR "/home/x/Install/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(APPLE)
        set(VCPKG_DIR "/Users/ms/vcpkg/" CACHE PATH "VCPKG installation directory")
    endif()
endif()

# --------------------------------------------------------------------
# Qt 版本配置（已固定 Qt6，不再支持 Qt5）
# --------------------------------------------------------------------
set(QT_VERSION_MAJOR 6)

# --------------------------------------------------------------------
# Qt 路径配置（默认值，可通过 Config.local.cmake 或 -DQt_INSTALL_DIR= 覆盖）
# --------------------------------------------------------------------
if(NOT Qt_INSTALL_DIR)
    if(WIN32)
        set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt installation directory")
    elseif(UNIX AND NOT APPLE)
        set(Qt_INSTALL_DIR "/home/x/Install/Qt/6.11.1/gcc_64" CACHE PATH "Qt installation directory")
    elseif(APPLE)
        set(Qt_INSTALL_DIR "/Users/ms/Qt/6.11.1/macos/" CACHE PATH "Qt installation directory")
    endif()
endif()

# --------------------------------------------------------------------
# 模块编译开关配置
# --------------------------------------------------------------------
# [A2 修复] 使用 option() 代替 set(... FORCE)，允许用户通过 -D<OPTION>=OFF 覆盖。
# 默认值可通过 Config.local.cmake 中的 SANYI_DEFAULT_* 变量自定义。
if(NOT DEFINED SANYI_DEFAULT_OPTIONAL)
    set(SANYI_DEFAULT_OPTIONAL ON)
endif()
if(NOT DEFINED SANYI_DEFAULT_CORE)
    set(SANYI_DEFAULT_CORE OFF)
endif()

option(BUILD_VISION "Build Vision module (image processing, computer vision)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_NETWORK "Build Network module (HTTP, WebSocket, cloud sync)" OFF)
option(BUILD_HARDWARE "Build Hardware module (laser control, material database)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_ENGRAVING "Build Engraving module (3D laser engraving)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_GEOMODELCORE "Build GeoModelCore module (OpenCASCADE-based geometry modeling)" ${SANYI_DEFAULT_CORE})
option(BUILD_UI3D "Build UI3D module (3D user interface)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_NESTING "Build Nesting module (2D/3D nesting/arrangement algorithm)" ${SANYI_DEFAULT_OPTIONAL})

# ====================================================================
# 自动配置区域（以下内容自动配置，无需手动修改）
# ====================================================================

# --------------------------------------------------------------------
# Qt CMake 配置目录（自动从 Qt_INSTALL_DIR 派生）
# --------------------------------------------------------------------
set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake configuration directory")

# --------------------------------------------------------------------
# 设置 CMAKE_PREFIX_PATH
# --------------------------------------------------------------------
list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")

# --------------------------------------------------------------------
# 输出Qt配置信息用于调试
# --------------------------------------------------------------------
get_filename_component(QT_VERSION_FULL "${Qt_INSTALL_DIR}" PATH)
get_filename_component(QT_VERSION_FULL "${QT_VERSION_FULL}" NAME)
message(STATUS "[Qt]")
message(STATUS "  Version: ${QT_VERSION_FULL}")
message(STATUS "  Install Directory: ${Qt_INSTALL_DIR}")
message(STATUS "  Config Directory: ${Qt6_DIR}")
message(STATUS "  CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

# --------------------------------------------------------------------
# 自动配置 CMAKE_PREFIX_PATH 和 运行时环境
# --------------------------------------------------------------------
if(EXISTS "${Qt_INSTALL_DIR}")
    if(WIN32)
        set(_SANYI_QT_BIN "${Qt_INSTALL_DIR}/bin")
        set(_SANYI_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
        set(ENV{PATH} "${_SANYI_QT_BIN};${_SANYI_VCPKG_BIN};$ENV{PATH}")
        set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "Qt plugins directory")
        
        # Qt6 的 MOC/RCC 由 Qt6::moc / Qt6::rcc 目标自动管理，无需手动指定路径
    endif()
endif()
