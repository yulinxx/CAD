# ============================================================================
# 必需第三方依赖统一预检（configure 阶段）
# ----------------------------------------------------------------------------
# 目的：项目约定第三方库统一由 vcpkg 提供。任何一个必需端口缺失时，在进入各
# 模块 add_subdirectory 之前一次性汇总报出来，并给出可直接复制的安装命令
# （端口名 + 当前构建用的 triplet），而不是让用户逐个模块撞 find_package 的
# 裸报错、更不是拖到编译阶段才 fatal error（例如只有头文件的端口）。
#
# 设计：
#   1. 端口清单在本文件维护；仓库根 vcpkg.json 负责锁定版本（manifest 模式），
#      本文件负责按模块开关做运行时门禁（例如仅 BUILD_GEOMODELCORE=ON 才要
#      opencascade）。新增依赖时两处都要登记。
#   2. 探测方式：
#        package —— find_package(<包名> [CONFIG|MODULE] QUIET)
#        header  —— find_path(<头文件名>)，只用于"纯头文件端口"（无 CMake config）
#   3. 只探测、不重复加载：命中的包在后面模块里的 find_package 会直接走缓存；
#      未命中则汇总后 FATAL_ERROR，configure 明确失败（不降级、不改可选）。
#   4. 与 CMake/Utils.cmake 的 apr_require_vcpkg_package 的关系：预检是全局
#      权威门禁，正常整仓 configure 时先失败，模块级提示不会出现；后者保留为
#      模块单独 configure / 新增依赖漏登记时的兜底，两者文案口径一致。
#
# 条目格式（用 | 分隔，避免 CMake 列表转义）：
#   package|<find_package 包名>|<vcpkg 端口>|<查找模式 CONFIG/MODULE/空>|<条件表达式/空>
#   header|<头文件名>|<vcpkg 端口>|<>|<条件表达式/空>
#
# 条件必须写成单个变量名（例如 BUILD_TESTS、_need_clipper2），不能写
# "A OR B" 这类表达式：if() 不会对变量展开出来的多词表达式再次解析。
# 需要的组合条件在 apr_check_vcpkg_dependencies 里先折算成门控变量。
# ============================================================================

# 取当前构建对应的 vcpkg triplet，仅用于提示文案。
# vcpkg 工具链加载后会定义 VCPKG_TARGET_TRIPLET（未显式 -D 时也由工具链推导），
# 未加载工具链时按平台/架构近似推导 vcpkg 默认 triplet。
function(_apr_vcpkg_current_triplet _out_var)
    if(VCPKG_TARGET_TRIPLET)
        set(${_out_var} "${VCPKG_TARGET_TRIPLET}" PARENT_SCOPE)
        return()
    endif()

    set(_triplet "")
    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES)
            # 通用二进制（同时含 arm64 与 x86_64）无法映射到单一 triplet，保持为空
            if("arm64" IN_LIST CMAKE_OSX_ARCHITECTURES
               AND NOT "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
                set(_triplet "arm64-osx")
            elseif("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES
                   AND NOT "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES)
                set(_triplet "x64-osx")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(_triplet "arm64-osx")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
            set(_triplet "x64-osx")
        endif()
    elseif(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_triplet "x64-windows")
        else()
            set(_triplet "x86-windows")
        endif()
    elseif(UNIX)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(_triplet "arm64-linux")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
            set(_triplet "x64-linux")
        endif()
    endif()

    set(${_out_var} "${_triplet}" PARENT_SCOPE)
endfunction()

# 计算 vcpkg 头文件根目录（纯头文件端口用），取不到时返回空。
function(_apr_vcpkg_include_root _out_var)
    set(_root "")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        set(_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
        if(NOT EXISTS "${_root}")
            set(_root "")
        endif()
    endif()
    set(${_out_var} "${_root}" PARENT_SCOPE)
endfunction()

# 清单：模块实际用到的必需 vcpkg 端口（新增依赖时在这里登记）
function(_apr_vcpkg_required_dependencies _out_var)
    set(_deps
        # EngineCommon：几何算法与字体
        "package|Boost|boost|CONFIG|"
        "package|freetype|freetype|CONFIG|"
        # EngineCommon 头文件型依赖：只有头文件、没有 CMake config，
        # 必须在这里拦住，否则会一路拖到编译期 fatal error
        "header|BS_thread_pool.hpp|bshoshany-thread-pool||"
        # FileIO
        "package|protobuf|protobuf|CONFIG|"
        "package|NanoSVG|nanosvg|CONFIG|"
        "package|Stb|stb||"
        "package|WebP|libwebp||"
        "package|TIFF|tiff||"
        # UI
        "package|SQLiteCpp|sqlitecpp|CONFIG|"
        # Engraving / Nesting（Clipper2 与 OpenCV）
        "package|Clipper2|clipper2||_need_clipper2"
        "package|OpenCV|opencv4|CONFIG|_need_opencv"
        # Log / Licensing
        "package|spdlog|spdlog|CONFIG|"
        "package|OpenSSL|openssl|CONFIG|"
        # 测试
        "package|GTest|gtest|CONFIG|BUILD_TESTS"
        # GeoModelCore（OpenCASCADE）
        "package|OpenCASCADE|opencascade|CONFIG|BUILD_GEOMODELCORE"
    )
    set(${_out_var} "${_deps}" PARENT_SCOPE)
endfunction()

function(apr_check_vcpkg_dependencies)
    # 条件必须是"单个变量名"（if() 不会对展开出来的多词表达式再次解析，
    # 所以像 "A OR B" 这样的表达式必须先折算成下面的门控变量）
    set(_need_clipper2 FALSE)
    if(BUILD_ENGRAVING OR BUILD_NESTING OR BUILD_CAM)
        set(_need_clipper2 TRUE)
    endif()
    set(_need_opencv FALSE)
    if(BUILD_ENGRAVING OR BUILD_VISION OR BUILD_RASTER)
        set(_need_opencv TRUE)
    endif()

    _apr_vcpkg_current_triplet(_triplet)
    _apr_vcpkg_include_root(_vcpkg_include_root)

    _apr_vcpkg_required_dependencies(_deps)
    set(_missing "")
    set(_missing_ports "")
    set(_checked 0)

    foreach(_entry IN LISTS _deps)
        string(REPLACE "|" ";" _fields "${_entry}")
        list(GET _fields 0 _kind)
        list(GET _fields 1 _probe)
        list(GET _fields 2 _port)
        list(GET _fields 3 _mode)
        list(GET _fields 4 _condition)

        # 条件为空表示必需；否则按 if() 表达式求值
        if(_condition)
            if(NOT ${_condition})
                continue()
            endif()
        endif()

        math(EXPR _checked "${_checked} + 1")

        if(_kind STREQUAL "header")
            # 纯头文件端口：只在头文件搜索路径里找，避免依赖编译器探测污染缓存
            if(_vcpkg_include_root AND EXISTS "${_vcpkg_include_root}/${_probe}")
                continue()
            endif()
            find_path(_apr_header_dir "${_probe}")
            if(_apr_header_dir)
                continue()
            endif()
            list(APPEND _missing "    - ${_port} (header \"${_probe}\" not found)")
            list(APPEND _missing_ports "${_port}")
            unset(_apr_header_dir CACHE)
        else()
            if(_mode)
                find_package(${_probe} ${_mode} QUIET)
            else()
                find_package(${_probe} QUIET)
            endif()
            if(${_probe}_FOUND)
                continue()
            endif()
            list(APPEND _missing "    - ${_port} (CMake package \"${_probe}\" not found)")
            list(APPEND _missing_ports "${_port}")
        endif()
    endforeach()

    if(NOT _missing)
        message(STATUS "[April] vcpkg dependency preflight: OK (${_checked} required ports available for triplet ${_triplet})")
        return()
    endif()

    # 组装安装命令：只装缺失端口，triplet 与当前构建一致
    set(_install_missing "")
    foreach(_port IN LISTS _missing_ports)
        if(_triplet)
            list(APPEND _install_missing "${_port}:${_triplet}")
        else()
            list(APPEND _install_missing "${_port}")
        endif()
    endforeach()
    if(_triplet)
        set(_triplet_summary "${_triplet}")
    else()
        set(_triplet_summary "unknown (append :<triplet>, e.g. <port>:arm64-osx)")
    endif()
    string(REPLACE ";" " " _install_expr "${_install_missing}")

    set(_vcpkg_root "")
    if(Z_VCPKG_ROOT_DIR)
        set(_vcpkg_root "${Z_VCPKG_ROOT_DIR}")
    elseif(VCPKG_DIR)
        string(REGEX REPLACE "[/\\\\]+$" "" _vcpkg_root "${VCPKG_DIR}")
    endif()
    if(NOT _vcpkg_root)
        set(_vcpkg_root "<vcpkg-root>")
    endif()

    set(_toolchain "${CMAKE_TOOLCHAIN_FILE}")
    if(NOT _toolchain)
        set(_toolchain "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
    endif()

    string(REPLACE ";" "\n" _missing_lines "${_missing}")

    set(_msg "")
    string(APPEND _msg "\n[April] vcpkg dependency preflight failed: ${_checked} required port(s) checked, some are missing.\n\n")
    string(APPEND _msg "  Missing dependencies:\n")
    string(APPEND _msg "${_missing_lines}\n\n")
    string(APPEND _msg "  Install all missing ports with:\n\n")
    string(APPEND _msg "      vcpkg install ${_install_expr}\n\n")
    string(APPEND _msg "  If vcpkg is not on PATH, run it from the vcpkg root instead:\n\n")
    string(APPEND _msg "      ${_vcpkg_root}/vcpkg install ${_install_expr}\n\n")
    string(APPEND _msg "  Then re-run the CMake configure step with the same toolchain and triplet:\n\n")
    string(APPEND _msg "      cmake -S ${CMAKE_SOURCE_DIR} -B <build-dir>"
                       " -DCMAKE_TOOLCHAIN_FILE=${_toolchain}"
                       " -DVCPKG_TARGET_TRIPLET=${_triplet}\n\n")
    string(APPEND _msg "  Current configuration:\n")
    string(APPEND _msg "    triplet:    ${_triplet_summary}\n")
    string(APPEND _msg "    vcpkg root: ${_vcpkg_root}\n\n")
    string(APPEND _msg "  Notes:\n")
    string(APPEND _msg "    - The triplet must match this configuration; libraries installed for a\n")
    string(APPEND _msg "      different triplet are invisible to CMake.\n")
    string(APPEND _msg "    - Ports without a CMake config (header-only) are verified by their header\n")
    string(APPEND _msg "      file, so they are reported here instead of failing at compile time.\n")
    string(APPEND _msg "    - These dependencies are required and cannot be skipped or made optional.\n")

    message(STATUS "${_msg}")
    message(FATAL_ERROR
        "[April] configure failed: missing vcpkg dependencies (install command is printed above)")
endfunction()
