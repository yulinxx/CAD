# =============================================================================
# QualityGates.cmake
#
# 第一方目标的告警与 sanitizer 开关。
#
# 被 CMake/Utils.cmake（集成构建）和 CMake/StandaloneInit.cmake（独立模块构建）
# 共同 include，保证两条构建路径语义一致。
#
# 为什么不放在根 CMakeLists/Config.cmake 里用 add_compile_options：
#   1. 全局 add_compile_options 会作用到 ThirdParty（libdxfrw 等），三方告警
#      不归本项目修，也不该阻断第一方构建；
#   2. 测试目标由各模块直接 add_executable 创建，不经过共享库辅助函数，
#      所以必须在所有 add_subdirectory 结束后统一遍历目标挂载。
# =============================================================================

# Utils.cmake 会被多个模块重复 include；本文件只应生效一次
if(APR_QUALITY_GATES_INCLUDED)
    return()
endif()
set(APR_QUALITY_GATES_INCLUDED TRUE)

option(APR_WERROR "Treat first-party compiler warnings as errors" ON)
# 默认排除项（每一项都必须写清原因，不允许无声豁免）：
#   Engine/2D、Engine/3D、UI/2D、UI/3D
#       正在由并行分支改写，开 -Werror 遇到告警就必须改这些目录的源码，
#       会与对方冲突。待其合并后从列表移除即可铺满全仓。
#   GeoModelCore/GeoModelCore
#       git submodule（独立仓库、固定 SHA）。其中的 OCCT 7.8 废弃 API
#       （GetMessageString / TopTools_HSequenceOfShape / ConnectEdgesToWires）
#       只能在子模块仓库里修，改了主仓库记录的 SHA 会让 CI 的
#       submodules: recursive 拉不到提交。待子模块侧修完再移除本项。
# 默认排除列表用普通变量而不是 cache：默认值随代码更新，旧构建目录不会残留过期的豁免表。
# 需要临时扩大范围时用 -DAPR_WERROR_EXTRA_EXCLUDED_DIRS=<dir>[;<dir>...] 追加。
set(_APR_WERROR_EXCLUDED_DEFAULT
    "Engine/2D;Engine/3D;UI/2D;UI/3D;GeoModelCore/GeoModelCore")
set(APR_WERROR_EXTRA_EXCLUDED_DIRS "" CACHE STRING
    "Extra directories excluded from -Werror (semicolon-separated)")
set(APR_WERROR_EXCLUDED_DIRS
    "${_APR_WERROR_EXCLUDED_DEFAULT};${APR_WERROR_EXTRA_EXCLUDED_DIRS}")

option(APR_SANITIZE "Enable compiler sanitizers (address,undefined | thread)" OFF)

function(_apr_apply_quality_gates_walk _bin_dir)
    get_property(_targets DIRECTORY "${_bin_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            continue()
        endif()

        get_target_property(_src_dir ${_t} SOURCE_DIR)
        if(NOT _src_dir)
            continue()
        endif()

        # 三方源码一律不加；导入目标不会出现在 BUILDSYSTEM_TARGETS 里，这里再兜一层
        if(_src_dir MATCHES "[/\\\\]ThirdParty([/\\\\]|$)" OR
           _src_dir MATCHES "[/\\\\]_deps([/\\\\]|$)")
            continue()
        endif()

        set(_excluded_from_werror FALSE)
        foreach(_ex IN LISTS APR_WERROR_EXCLUDED_DIRS)
            if(_src_dir MATCHES "[/\\\\]${_ex}$" OR _src_dir MATCHES "[/\\\\]${_ex}[/\\\\]")
                set(_excluded_from_werror TRUE)
                break()
            endif()
        endforeach()

        if(APR_WERROR AND NOT _excluded_from_werror)
            # 注意：目标里直接编入的三方源文件（如 FileIO 的 libdxfrw）不在这里处理。
            # 源文件属性是目录作用域，从根目录设置对子目录目标无效；
            # 这类需要由拥有目标的 CMakeLists 自行设置（见 FileIO/FileIO/CMakeLists.txt）。
            if(MSVC)
                target_compile_options(${_t} PRIVATE /WX)
            else()
                target_compile_options(${_t} PRIVATE -Werror)
            endif()
        endif()

        if(APR_SANITIZE)
            if(MSVC)
                message(FATAL_ERROR
                    "[QualityGates] APR_SANITIZE is not supported with MSVC. "
                    "Use the manual sanitizer workflow (clang/gcc) instead.")
            endif()
            target_compile_options(${_t} PRIVATE
                "-fsanitize=${APR_SANITIZE}" -fno-omit-frame-pointer -g)
            target_link_options(${_t} PRIVATE "-fsanitize=${APR_SANITIZE}")
        endif()
    endforeach()

    get_property(_subdirs DIRECTORY "${_bin_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _apr_apply_quality_gates_walk("${_sub}")
    endforeach()
endfunction()

# 根目录处理完所有 add_subdirectory 后再统一挂载
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL _apr_apply_quality_gates_walk "${CMAKE_BINARY_DIR}")

if(APR_WERROR)
    message(STATUS "[QualityGates] -Werror enabled for first-party targets "
        "(excluding: ${APR_WERROR_EXCLUDED_DIRS})")
endif()
if(APR_SANITIZE)
    message(STATUS "[QualityGates] Sanitizers: ${APR_SANITIZE}")
endif()
