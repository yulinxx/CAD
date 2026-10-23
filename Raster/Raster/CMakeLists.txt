cmake_minimum_required(VERSION 4.3)

# =============================================================================
# Raster —— 位图（栅格）处理库
#
# 面向激光雕刻 / 激光切割工艺：预处理、二值化、半调、轮廓提取、割缝补偿、
# 统计质检。独立 DLL，公开头只暴露纯 C ABI（POD + 句柄），
# 消费方不需要 OpenCV 头文件、不需要 C++ 运行时。
# =============================================================================

# --- 模块元信息 ---
set(LIB_NAME "Raster")
set(LIB_FOLDER "Modules")
set(LIB_DESCRIPTION "Raster Bitmap Processing Library (Laser Engraving / Cutting)")
set(LIB_EXPORT_MACRO "RASTER_EXPORTS")

# --- 路径 ---
if(NOT SANYI_ROOT)
    get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RasterAbiGuard.cmake")
include(GNUInstallDirs)

# --- 版本（按模块独立）---
# 1.0.0: 首版。C ABI v1，位图预处理 + 激光工艺算子 + 质检 + 管线。
#
# 版本号的**唯一事实源**是公开头 RasterTypes.h 里的三个宏：头文件要进 SDK，
# 消费方会直接读它；如果版本只在 CMake 里定义，头与构建产物就会各说各话。
# 所以这里从头文件里解析，而不是在这里另写一份。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Include/Raster/RasterTypes.h" _raster_types_header)
string(REGEX MATCH "RASTER_VERSION_MAJOR[ \t]+([0-9]+)" _raster_maj "${_raster_types_header}")
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "RASTER_VERSION_MINOR[ \t]+([0-9]+)" _raster_min "${_raster_types_header}")
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "RASTER_VERSION_PATCH[ \t]+([0-9]+)" _raster_pat "${_raster_types_header}")
set(VERSION_PATCH "${CMAKE_MATCH_1}")
if(NOT _raster_maj OR NOT _raster_min OR NOT _raster_pat)
    message(FATAL_ERROR
        "[Raster] cannot parse RASTER_VERSION_MAJOR/MINOR/PATCH from "
        "Include/Raster/RasterTypes.h -- the header is the single source of truth "
        "for the version, fix the #defines there.")
endif()
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# --- 跨 DLL ABI 守卫 ---
# 扫描公开头文件：出现 OpenCV / STL 容器 / 异常 / 动态类型直接配置失败。
# 这条约定是"供其它工程（含第三方）调用"的前提，必须由构建系统强制。
option(RASTER_ABI_GUARD "Fail configuration when Raster public headers break the cross-DLL contract" ON)
if(RASTER_ABI_GUARD)
    sanyi_raster_abi_guard(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include/Raster")
endif()

# --- 源文件 ---
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c"
)
file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)
list(FILTER LIB_SOURCES EXCLUDE REGEX "${SANYI_SOURCE_EXCLUDE_REGEX}")
list(FILTER LIB_HEADERS EXCLUDE REGEX "${SANYI_HEADER_EXCLUDE_REGEX}")

# --- 第三方依赖：OpenCV ---
# OpenCV 是本模块唯一的第三方依赖，且**只在 DLL 内部使用**：
# 公开头不含 opencv2/，消费方既不需要 OpenCV 头文件也不需要链接 OpenCV。
#
# 刻意**不**用 find_package(OpenCV COMPONENTS ...)：不同平台 / 不同 OpenCV
# 发行版对组件名的声明不一致（vcpkg 的 opencv4 列出全部模块，而某些源码
# 构建只导出用到的那些），写死 COMPONENTS 会在某个平台上直接 configure 失败。
# 改成"整体找到 OpenCV，再自己挑需要的模块目标"，缺哪个降级到 OpenCV_LIBS。
find_package(OpenCV REQUIRED)

# 只链接实际用到的五个模块（core/imgproc/imgcodecs/photo/objdetect）：
#   core      基础数据结构
#   imgproc   滤波 / 形态学 / 几何变换 / 直方图
#   imgcodecs 读写 PNG/BMP/TIFF/JPEG
#   photo     fastNlMeansDenoising 去噪
#   objdetect QR / 一维码识别
# 避免把 dnn / videoio 这些用不到的模块拖进部署目录
# （objdetect 本身会传递依赖 dnn，见 RASTER_INSTALL_OPENCV_RUNTIME 的闭包处理）。
set(RASTER_OPENCV_LIBS "")
foreach(_ocv_component IN ITEMS opencv_core opencv_imgproc opencv_imgcodecs opencv_photo opencv_objdetect)
    if(TARGET ${_ocv_component})
        list(APPEND RASTER_OPENCV_LIBS ${_ocv_component})
    endif()
endforeach()
if(NOT RASTER_OPENCV_LIBS)
    set(RASTER_OPENCV_LIBS ${OpenCV_LIBS})
endif()
message(STATUS "  [Raster] OpenCV components: ${RASTER_OPENCV_LIBS}")

# --- 统一入口创建 DLL ---
sanyi_add_shared_library(${LIB_NAME}
    FOLDER "${LIB_FOLDER}"
    DESCRIPTION "${LIB_DESCRIPTION}"
    EXPORT_MACRO "${LIB_EXPORT_MACRO}"
    VERSION_MAJOR ${VERSION_MAJOR}
    VERSION_MINOR ${VERSION_MINOR}
    VERSION_PATCH ${VERSION_PATCH}
    SOURCES ${LIB_SOURCES}
    HEADERS ${LIB_HEADERS}
    PUBLIC_INCLUDE_DIRS
        # 只暴露 Include：消费方一律写 #include <Raster/RasterApi.h>，
        # 避免把 Include/Raster 也加进搜索路径后出现 RasterExport.h 的裸名污染。
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Src"
    LINK_LIBRARIES
        PRIVATE
            ${RASTER_OPENCV_LIBS}
)

# 非 Windows 平台默认隐藏符号，只导出带 visibility("default") 的 C API。
if(UNIX)
    set_target_properties(${LIB_NAME} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        # 安装后的库要能在自己的目录里找到同目录的依赖（动态 OpenCV）。
        # $ORIGIN 让加载器按"库文件所在目录"解析，而不是依赖 LD_LIBRARY_PATH ——
        # 后者对交付给客户的 SDK 是不成立的假设。
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH OFF
    )
endif()

# NOMINMAX：Windows 的 windows.h 把 min/max 定义成宏，会把 std::min / std::max
# 展开成 ((a) < (b) ? (a) : (b))，在 "std::" 后面就是语法错误。
# OpenCV 的头会拉进 windows.h，所以必须在编译本模块时关掉这两个宏。
if(MSVC)
    target_compile_definitions(${LIB_NAME} PRIVATE NOMINMAX)
endif()

# 关闭 unity build。
# 仓库全局开着 CMAKE_UNITY_BUILD（编译更快），但 unity 会把多个 .cpp 合进同一个
# 翻译单元：各文件匿名命名空间里的同名辅助函数会撞出 redefinition。
# 本模块的源文件之间有共享的内部辅助（已尽量提到 RasterInternal.h），
# 但为了不给后来人埋坑，这里显式关掉——模块只有 8 个源文件，收益远小于风险。
set_property(TARGET ${LIB_NAME} PROPERTY UNITY_BUILD OFF)

# --- 版本宏 ---
# 刻意**不**在这里 target_compile_definitions 版本宏：
# 公开头 RasterTypes.h 已经定义好了，再定义一份就会出现两处事实源，
# 迟早会漂移。消费方 include 头就能拿到全部版本信息。

# --- SDK 安装 ---------------------------------------------------------------
# 只在独立构建入口开启。集成构建下由根 CMakeLists.txt 的
# install(TARGETS ${SANYI_PROJECT_LIBS}) 统一安装，两处都装等于重复交付。
#
# 交付布局（消费方视角）：
#   include/Raster/{RasterApi.h,RasterTypes.h,RasterExport.h,RasterCpp.h}
#   lib/Raster.lib              （MSVC 导入库；Debug 为 Raster_d.lib）
#   bin/Raster.dll              （Debug 为 Raster_d.dll）
#   lib/cmake/Raster/*.cmake     （find_package(Raster CONFIG) 的接口）
if(RASTER_SDK_INSTALL)
    install(TARGETS ${LIB_NAME}
        EXPORT ${LIB_NAME}Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # 只装公开头文件目录；Src/ 下的内部头一律不出去。
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Include/Raster"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )

    # 第三方归属声明：随包分发 OpenCV 二进制时必须带上（Apache-2.0 §4）。
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LIB_NAME}")

    # 示例源码随包分发：客户拿到包就能直接编译第一个程序，
    # 不用去翻文档猜 include/lib 怎么接 —— 示例本身就是可编译的契约说明书。
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Example_Basic.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Example_LaserCut.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Example_Pipeline.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Example_CApi.c"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LIB_NAME}/examples")

    install(EXPORT ${LIB_NAME}Targets
        FILE ${LIB_NAME}Targets.cmake
        NAMESPACE ${LIB_NAME}::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LIB_NAME}"
    )

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}ConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake"
        "# Generated by Raster/CMakeLists.txt - DO NOT EDIT\n"
        "include(CMakeFindDependencyMacro)\n"
        "include(\"\${CMAKE_CURRENT_LIST_DIR}/${LIB_NAME}Targets.cmake\")\n"
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}ConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${LIB_NAME}"
    )

    # OpenCV 运行时：vcpkg 装的是动态库，Raster.dll 运行时依赖 opencv_*.dll。
    # 默认**不**随包分发（许可与体积问题由使用方决定），
    # 需要单文件夹自包含时打开 -DRASTER_INSTALL_OPENCV_RUNTIME=ON。
    option(RASTER_INSTALL_OPENCV_RUNTIME
        "Copy the OpenCV runtime DLLs next to the installed Raster library" OFF)
    if(RASTER_INSTALL_OPENCV_RUNTIME)
        # 自包含交付：把 OpenCV 运行时（含传递依赖）一起装到交付目录。
        #
        # 为什么不能只装直接依赖的模块（Windows）：
        #   opencv_objdetect4.dll 传递依赖 opencv_dnn4 / calib3d4 /
        #   features2d4 / flann4；opencv_imgcodecs4.dll 又依赖
        #   jpeg62 / libwebp* / libpng16 / tiff / zlib1。
        # 少装任何一个，客户那边的程序都会以 STATUS_DLL_NOT_FOUND(0xC0000135)
        # 起不来，而且报错完全指不到真正缺的是哪一个。
        #
        # 闭包从 OpenCV 导入目标的 INTERFACE_LINK_LIBRARIES 求 —— 那是 OpenCV
        # 自己的模块依赖图，与运行时导入表一致，但不需要扫系统目录（快得多）。
        set(_raster_ocv_closure "")
        set(_raster_ocv_frontier ${RASTER_OPENCV_LIBS})
        while(_raster_ocv_frontier)
            list(POP_FRONT _raster_ocv_frontier _ocv_mod)
            if("${_ocv_mod}" IN_LIST _raster_ocv_closure)
                continue()
            endif()
            list(APPEND _raster_ocv_closure "${_ocv_mod}")
            if(TARGET ${_ocv_mod})
                get_target_property(_ocv_mod_libs ${_ocv_mod} INTERFACE_LINK_LIBRARIES)
                if(_ocv_mod_libs)
                    foreach(_ocv_dep IN LISTS _ocv_mod_libs)
                        if(TARGET ${_ocv_dep})
                            list(APPEND _raster_ocv_frontier ${_ocv_dep})
                        endif()
                    endforeach()
                endif()
            endif()
        endwhile()

        if(WIN32)
            # Windows：交付物是 bin/ 下的 .dll。
            set(_raster_ocv_runtime_dlls "")
            foreach(_ocv_mod IN LISTS _raster_ocv_closure)
                get_filename_component(_ocv_mod_name "${_ocv_mod}" NAME)
                file(GLOB _ocv_dlls "${OpenCV_INSTALL_PATH}/bin/${_ocv_mod_name}*.dll")
                list(APPEND _raster_ocv_runtime_dlls ${_ocv_dlls})
            endforeach()
            # OpenCV 的 world 构建：所有模块合成一个 opencv_world*.dll。
            file(GLOB _ocv_world_dlls "${OpenCV_INSTALL_PATH}/bin/opencv_world*.dll")
            list(APPEND _raster_ocv_runtime_dlls ${_ocv_world_dlls})

            # OpenCV 自己依赖的第三方库：它们不叫 opencv_*，但缺了同样起不来。
            #
            # 这份名单是**显式维护**的，原因：OpenCV 的模块依赖图
            # （INTERFACE_LINK_LIBRARIES）只覆盖 opencv_* 之间的依赖，
            # 覆盖不到 opencv_dnn4 依赖的 libprotobuf / abseil，
            # 也覆盖不到 libwebp -> libsharpyuv、tiff -> liblzma 这类二级依赖。
            #
            # 为什么不用 file(GET_RUNTIME_DEPENDENCIES) 走导入表闭包：
            # 那条路要扫系统目录，单个 DLL 就要 20 秒，20 个 DLL 就是 7 分钟，
            # 放进安装步骤里不可接受。
            #
            # 名单的正确性由**交付门禁**保证：Tools/VerifySdk.ps1 会拿装好的 SDK
            # 跑一个真实的外部消费程序；少一个 DLL 它就会以
            # STATUS_DLL_NOT_FOUND(0xC0000135) 起不来，门禁立刻失败。
            # 所以"名单过期"不可能悄悄溜过去。
            foreach(_ocv_thirdparty IN ITEMS
                    "jpeg62" "libpng16" "libwebp" "libwebpdecoder" "libwebpdemux"
                    "libwebpmux" "libsharpyuv" "tiff" "zlib1" "liblzma"
                    "libprotobuf" "abseil_dll")
                file(GLOB _tp_dlls "${OpenCV_INSTALL_PATH}/bin/${_ocv_thirdparty}*.dll")
                list(APPEND _raster_ocv_runtime_dlls ${_tp_dlls})
            endforeach()
            list(REMOVE_DUPLICATES _raster_ocv_runtime_dlls)

            if(_raster_ocv_runtime_dlls)
                message(STATUS "  [Raster] OpenCV runtime to install: ${_raster_ocv_runtime_dlls}")
                install(FILES ${_raster_ocv_runtime_dlls}
                    DESTINATION "${CMAKE_INSTALL_BINDIR}"
                    COMPONENT Runtime)
            else()
                message(WARNING
                    "[Raster] RASTER_INSTALL_OPENCV_RUNTIME=ON but no OpenCV runtime DLL was "
                    "found under ${OpenCV_INSTALL_PATH}/bin -- the installed Raster.dll will "
                    "not be self-contained. Check the OpenCV installation layout.")
            endif()
        else()
            # Unix / macOS：vcpkg 下 OpenCV 默认**静态**链接，libRaster.so 本身
            # 没有 OpenCV 依赖，不需要随包分发。
            # 但对着系统安装的**动态** OpenCV（libopencv_core.so.4.x），
            # 就必须把 .so 一起装到 lib/ 并给 Raster 加 $ORIGIN RPATH，
            # 否则加载器找不到依赖。
            set(_raster_ocv_runtime_dlls "")
            foreach(_ocv_mod IN LISTS _raster_ocv_closure)
                get_filename_component(_ocv_mod_name "${_ocv_mod}" NAME)
                file(GLOB _ocv_libs
                    "${OpenCV_INSTALL_PATH}/lib/lib${_ocv_mod_name}.so*"
                    "${OpenCV_INSTALL_PATH}/lib/lib${_ocv_mod_name}.dylib")
                list(APPEND _raster_ocv_runtime_dlls ${_ocv_libs})
            endforeach()
            file(GLOB _ocv_world_libs
                "${OpenCV_INSTALL_PATH}/lib/libopencv_world.so*"
                "${OpenCV_INSTALL_PATH}/lib/libopencv_world.dylib")
            list(APPEND _raster_ocv_runtime_dlls ${_ocv_world_libs})
            list(REMOVE_DUPLICATES _raster_ocv_runtime_dlls)

            if(_raster_ocv_runtime_dlls)
                message(STATUS "  [Raster] OpenCV shared libraries to install: ${_raster_ocv_runtime_dlls}")
                install(FILES ${_raster_ocv_runtime_dlls}
                    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                    COMPONENT Runtime)
            endif()
        endif()
    endif()
endif()

# --- 单元测试 ---
# 由根 CMakeLists 的 BUILD_RASTER_TESTS 控制。
if(BUILD_RASTER_TESTS)
    if(TARGET GTest::gtest OR GTest_FOUND)
        add_subdirectory(Test)
    else()
        message(STATUS "  [Raster] BUILD_RASTER_TESTS=ON but GTest is unavailable; tests skipped")
    endif()
endif()
