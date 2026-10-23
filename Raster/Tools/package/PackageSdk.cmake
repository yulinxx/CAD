# =============================================================================
# PackageSdk.cmake
#
# 把装好的 SDK 打成一个可交付的压缩包，并生成清单。
#
# 用法（cmake -P 脚本模式，跨平台，不需要额外工具）：
#
#   cmake -DSDK_ROOT=<install prefix> \
#         -DSDK_OUTPUT=<输出 zip 路径> \
#         -DSDK_ARCH=<架构标签> \
#         -DSDK_VERSION=<版本> \
#         -P Raster/Tools/package/PackageSdk.cmake
#
# 产出物：
#   <name>-<version>-<arch>.zip
#     ├── include/Raster/*.h        公开头
#     ├── lib/ | bin/               库与运行时
#     ├── lib/cmake/Raster/         CMake 包配置
#     ├── share/doc/Raster/         NOTICE + 示例源码
#     ├── MANIFEST.txt              每个文件的 SHA-256 与大小
#     └── VERSION.txt               模块版本 / ABI / 构建信息
#
# 为什么要有 MANIFEST：
#   交付出去的二进制是**不可复现**的（编译期、工具链、优化选项都会变）。
#   收到包的人需要能独立验证"这个文件确实是我给你的那个"，
#   以及"我手上的包和你的包是同一个"。SHA-256 清单是最省事的答案。
#
# 为什么用 CMake 脚本而不是 PowerShell / shell：
#   交付打包这一步必须在 Windows / Linux / macOS 上给出**同一个结果**。
#   CMake 脚本天生跨平台，也不用在 CI 里维护两套打包逻辑。
# =============================================================================

cmake_minimum_required(VERSION 3.20)

foreach(_required SDK_ROOT SDK_OUTPUT SDK_ARCH SDK_VERSION)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "PackageSdk.cmake: -D${_required} is required")
    endif()
endforeach()

get_filename_component(_sdk_root "${SDK_ROOT}" ABSOLUTE)
get_filename_component(_sdk_output "${SDK_OUTPUT}" ABSOLUTE)
if(NOT IS_DIRECTORY "${_sdk_root}")
    message(FATAL_ERROR "PackageSdk.cmake: SDK_ROOT '${_sdk_root}' is not a directory")
endif()

get_filename_component(_output_dir "${_sdk_output}" DIRECTORY)
get_filename_component(_output_name "${_sdk_output}" NAME)
# 去掉扩展名，得到包目录名：Raster-1.0.0-x64-windows.zip -> Raster-1.0.0-x64-windows
#
# 注意：**不能**用 get_filename_component(... NAME_WE)。CMake 的 NAME_WE 是
# "去掉最长扩展名"，对 "Raster-1.0.0-x64-windows.zip" 会把
# ".0.0-x64-windows.zip" 当扩展名剥掉，只剩 "Raster-1" —— 版本号被吃掉，
# 而且不同版本打出来的包目录名一样，极易拿错。
string(REGEX REPLACE "\\.[Zz][Ii][Pp]$" "" _package_stem "${_output_name}")
if(_package_stem STREQUAL "${_output_name}")
    # 没有 .zip 后缀：把整个名字当目录名
    set(_package_stem "${_output_name}")
endif()
set(_package_dir "${_output_dir}/${_package_stem}")

# ---------------------------------------------------------------------------
# 1. 拷贝到暂存目录
# ---------------------------------------------------------------------------
file(REMOVE_RECURSE "${_package_dir}")
file(MAKE_DIRECTORY "${_package_dir}")

file(GLOB_RECURSE _sdk_files
    "${_sdk_root}/include/*"
    "${_sdk_root}/lib/*"
    "${_sdk_root}/bin/*"
    "${_sdk_root}/share/*")
if(NOT _sdk_files)
    message(FATAL_ERROR
        "PackageSdk.cmake: nothing to package under '${_sdk_root}' "
        "(expected include/, lib/, bin/ or share/)")
endif()

foreach(_file IN LISTS _sdk_files)
    file(RELATIVE_PATH _rel "${_sdk_root}" "${_file}")
    get_filename_component(_rel_dir "${_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_package_dir}/${_rel_dir}")
    file(COPY "${_file}" DESTINATION "${_package_dir}/${_rel_dir}")
endforeach()

# ---------------------------------------------------------------------------
# 2. VERSION.txt —— 交付件的身份信息
# ---------------------------------------------------------------------------
string(TIMESTAMP _build_date "%Y-%m-%d" UTC)

# cmake -P 脚本模式下 CMAKE_GENERATOR / CMAKE_HOST_SYSTEM_PROCESSOR 是空的，
# 所以这两项由调用方传入（打包方知道自己的工具链）。
if(NOT DEFINED SDK_GENERATOR OR SDK_GENERATOR STREQUAL "")
    set(SDK_GENERATOR "n/a")
endif()
if(NOT DEFINED SDK_HOST OR SDK_HOST STREQUAL "")
    set(SDK_HOST "${CMAKE_HOST_SYSTEM_NAME}")
endif()

set(_version_txt
"Raster SDK
===========

module version : ${SDK_VERSION}
ABI version    : 1
architecture   : ${SDK_ARCH}
packaged (UTC) : ${_build_date}
generator      : ${SDK_GENERATOR}
host system    : ${SDK_HOST}
")
file(WRITE "${_package_dir}/VERSION.txt" "${_version_txt}")

# ---------------------------------------------------------------------------
# 3. MANIFEST.txt —— 每个文件的 SHA-256 与大小
# ---------------------------------------------------------------------------
file(GLOB_RECURSE _package_files "${_package_dir}/*")
list(SORT _package_files)

set(_manifest "# Raster SDK package manifest\n")
string(APPEND _manifest "# algorithm: SHA-256\n")
string(APPEND _manifest "# module version: ${SDK_VERSION}   arch: ${SDK_ARCH}\n")
string(APPEND _manifest "#\n")
string(APPEND _manifest "# <sha256>  <size>  <path relative to package root>\n")
string(APPEND _manifest "\n")

set(_total_size 0)
set(_file_count 0)
foreach(_file IN LISTS _package_files)
    if(IS_DIRECTORY "${_file}")
        continue()
    endif()
    file(SHA256 "${_file}" _sha)
    file(SIZE "${_file}" _size)
    file(RELATIVE_PATH _rel "${_package_dir}" "${_file}")
    # 路径分隔符统一成 /，让同一份清单在三个平台上可比对。
    string(REPLACE "\\" "/" _rel "${_rel}")
    string(APPEND _manifest "${_sha}  ${_size}  ${_rel}\n")
    math(EXPR _total_size "${_total_size} + ${_size}")
    math(EXPR _file_count "${_file_count} + 1")
endforeach()

string(APPEND _manifest "\n# ${_file_count} file(s), ${_total_size} byte(s) total\n")
file(WRITE "${_package_dir}/MANIFEST.txt" "${_manifest}")

# ---------------------------------------------------------------------------
# 4. 打包
# ---------------------------------------------------------------------------
# cmake -E tar 的 zip 格式要求：先进目录，再打里面的内容，
# 这样解出来的包根目录就是 <name>/ 而不是散落一地。
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${_sdk_output}" --format=zip
            "${_package_stem}"
    WORKING_DIRECTORY "${_output_dir}"
    RESULT_VARIABLE _tar_result
    OUTPUT_VARIABLE _tar_output
    ERROR_VARIABLE _tar_output)
if(NOT _tar_result EQUAL 0)
    message(FATAL_ERROR "PackageSdk.cmake: failed to create the archive\n${_tar_output}")
endif()

file(SIZE "${_sdk_output}" _zip_size)
message(STATUS "[PackageSdk] ${_sdk_output}")
message(STATUS "[PackageSdk]   ${_file_count} file(s), ${_total_size} byte(s) uncompressed, ${_zip_size} byte(s) zipped")
