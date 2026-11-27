# project() 之前确定的路径与 toolchain；由根 CMakeLists.txt 在 configure.cmake 之后、project() 之前 include。

if(WIN32)
    set(UI_Platform Windows)
elseif(APPLE)
    set(UI_Platform macOS)
else()
    set(UI_Platform Linux)
endif()

set(_UI_qt_prefix "${Path_Qt${QT_VERSION_MAJOR}_${UI_Platform}_64}")
set(_UI_vcpkg_root "${Path_vcpkg_${UI_Platform}}")

# 本工程只用 Qt，无 vcpkg 依赖，默认不启用 toolchain。
option(UI_USE_VCPKG "用 vcpkg toolchain 解析第三方依赖（本工程当前不需要）" OFF)

# vcpkg toolchain 必须在 project() 之前设置；父工程已传 toolchain 时保持不动。
if(UI_USE_VCPKG AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${_UI_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain (from configure.cmake)")
endif()

list(APPEND CMAKE_PREFIX_PATH "${_UI_qt_prefix}")
