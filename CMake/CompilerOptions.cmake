# ============================================================================
#  编译器特定配置（必须在 project() 之后 include）
# ============================================================================
#
# 为什么单独一个文件、而不是留在 Config.cmake 里：
#
#   `Config.cmake` 必须在 `project()` **之前** include（它设置
#   CMAKE_TOOLCHAIN_FILE，而工具链只有早于 project() 才会被采用）。
#   但 `CMAKE_CXX_COMPILER_ID` 是 `project()` 才设置的 —— 在它之前求值为空。
#   于是原先留在 Config.cmake 里的
#       if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#   永远不成立（含重新 configure，因为该变量不进缓存），
#   六条 -Wno-unused-* 与 macOS 的 -Wno-deprecated-declarations 从未生效。
#   实测：grep -c "Wno-unused-parameter" build/build.ninja  →  0
#
#   把依赖编译器识别的部分拆到这里、由 CMakeLists.txt 在 project() 之后 include，
#   判断才拿得到真实值。
#
# `add_compile_options` 作用于之后定义的所有 target，因此本文件的位置必须早于
# 任何 add_subdirectory —— 见 CMakeLists.txt 中的 include 点。
# ============================================================================

# ----------------------------------------------------------------------------
# MSVC
# ----------------------------------------------------------------------------
if(MSVC)
    # 动态链接 CRT (MD/MDd)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        CACHE STRING "MSVC runtime library")

    # UTF-8 编码支持
    add_compile_options(/utf-8)

    # 关闭 Unicode 字符警告（中文注释导致）
    add_compile_options(/wd4819)

    # 关闭 CRT 迭代扩展警告 (vcpkg 依赖大量模板导致)
    add_compile_options(/wd4503)

    message(STATUS "[Config] Compiler: MSVC ${MSVC_VERSION}")
endif()

# ----------------------------------------------------------------------------
# GCC / Clang
# ----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # 统一警告级别
    add_compile_options(-Wall -Wextra -Wpedantic)

    # 额外警告抑制 (跨平台兼容)
    add_compile_options(
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-private-field
        -Wno-unused-lambda-capture
        -Wno-nullability-extension
        -Wno-reorder
    )

    # macOS 特定
    if(APPLE)
        # 抑制大小写不敏感路径警告
        add_compile_options(-Wno-nonportable-include-path)
        # 抑制 OpenGL 废弃警告（使用 Metal 作为后端）
        add_compile_options(-Wno-deprecated-declarations)
    endif()

    message(STATUS "[Config] Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()
