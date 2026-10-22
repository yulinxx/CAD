# Raster SDK smoke test: C++ consumer via CMake package mode.
#
# This fixture stands in for the recommended integration path
# (find_package(Raster CONFIG REQUIRED) + Raster::Raster) and therefore also
# guards the installed export set, the package config files and the
# INSTALL_INTERFACE include directory.
#
# Required: -DCMAKE_PREFIX_PATH=<installed SDK prefix>
cmake_minimum_required(VERSION 3.20)
project(RasterSdkConsumerCpp LANGUAGES CXX)

find_package(Raster CONFIG REQUIRED)

add_executable(consumer_cpp main.cpp)
target_link_libraries(consumer_cpp PRIVATE Raster::Raster)

# 源文件里有中文注释：不加 /utf-8 时 MSVC 按 GBK(936) 解码，会给出 C4819
# 警告并且在个别字符上解码失败。
if(MSVC)
    target_compile_options(consumer_cpp PRIVATE /utf-8)
endif()

# The imported target does not put the runtime on PATH: deploy the SDK's whole
# runtime folder next to the exe (Raster.dll + the OpenCV runtime it ships).
add_custom_command(TARGET consumer_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Raster::Raster>"
        "$<TARGET_FILE_DIR:consumer_cpp>"
)
# Windows 的运行时在 bin/（.dll），Unix 的在 lib/（.so / .dylib）。
file(GLOB _raster_runtime_dlls
    "${CMAKE_PREFIX_PATH}/bin/*.dll"
    "${CMAKE_PREFIX_PATH}/lib/*.so*"
    "${CMAKE_PREFIX_PATH}/lib/*.dylib")
foreach(_dll IN LISTS _raster_runtime_dlls)
    add_custom_command(TARGET consumer_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:consumer_cpp>"
    )
endforeach()
