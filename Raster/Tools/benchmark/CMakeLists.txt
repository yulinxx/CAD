# Raster 吞吐基准
#
# 独立于模块本体：它只消费**公开 API**，所以能直接对着装好的 SDK 跑，
# 测出来的就是客户实际会感受到的性能。
#
# Required: -DRASTER_SDK_ROOT=<installed SDK prefix>
#   或者作为 Raster 独立构建的一部分（由 Raster/CMakeLists.txt 挂载，
#   此时直接链接本仓库的 Raster 目标）。
cmake_minimum_required(VERSION 3.20)
project(RasterBenchmark LANGUAGES CXX)

if(RASTER_BENCHMARK_USE_LOCAL_TARGET)
    # 独立构建：直接链接本仓库的 Raster 目标。
    add_executable(raster_benchmark benchmark.cpp)
    target_link_libraries(raster_benchmark PRIVATE Raster)
    target_compile_features(raster_benchmark PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(raster_benchmark PRIVATE /utf-8)
    endif()
else()
    if(NOT RASTER_SDK_ROOT)
        message(FATAL_ERROR "RASTER_SDK_ROOT is required (installed Raster SDK prefix)")
    endif()

    set(_raster_implib
        "${RASTER_SDK_ROOT}/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}Raster${CMAKE_IMPORT_LIBRARY_SUFFIX}")

    add_executable(raster_benchmark benchmark.cpp)
    target_include_directories(raster_benchmark PRIVATE "${RASTER_SDK_ROOT}/include")
    target_link_libraries(raster_benchmark PRIVATE "${_raster_implib}")
    target_compile_features(raster_benchmark PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(raster_benchmark PRIVATE /utf-8)
    endif()

    # 运行时：把 SDK 的 bin/ 一起部署过去，否则找不到 opencv_*.dll。
    file(GLOB _raster_runtime_dlls "${RASTER_SDK_ROOT}/bin/*.dll")
    foreach(_dll IN LISTS _raster_runtime_dlls)
        add_custom_command(TARGET raster_benchmark POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:raster_benchmark>"
        )
    endforeach()
endif()
