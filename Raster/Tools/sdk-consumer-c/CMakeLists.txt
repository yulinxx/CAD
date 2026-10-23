# Raster SDK smoke test: plain C consumer, manual include/lib wiring.
#
# This fixture intentionally does NOT use CMake package mode: it stands in for a
# consumer that only got the delivered include/, lib/ and bin/ folders. Building
# it as C is also the header's C-compatibility test -- if any C++ construct ever
# leaks into the public headers, this target stops compiling.
#
# Required: -DRASTER_SDK_ROOT=<installed SDK prefix>
cmake_minimum_required(VERSION 3.20)
project(RasterSdkConsumerC LANGUAGES C)

if(NOT RASTER_SDK_ROOT)
    message(FATAL_ERROR "RASTER_SDK_ROOT is required (installed Raster SDK prefix)")
endif()

set(_raster_implib
    "${RASTER_SDK_ROOT}/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}Raster${CMAKE_IMPORT_LIBRARY_SUFFIX}")
if(APPLE)
    set(_raster_runtime
        "${RASTER_SDK_ROOT}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}Raster${CMAKE_SHARED_LIBRARY_SUFFIX}")
elseif(UNIX)
    set(_raster_runtime
        "${RASTER_SDK_ROOT}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}Raster${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
    set(_raster_runtime
        "${RASTER_SDK_ROOT}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}Raster${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

if(NOT EXISTS "${_raster_implib}")
    message(FATAL_ERROR "import library not found: ${_raster_implib}")
endif()
if(NOT EXISTS "${_raster_runtime}")
    message(FATAL_ERROR "runtime library not found: ${_raster_runtime}")
endif()

add_executable(consumer_c main.c)
target_include_directories(consumer_c PRIVATE "${RASTER_SDK_ROOT}/include")
target_link_libraries(consumer_c PRIVATE "${_raster_implib}")

# The DLLs are not on PATH in a clean environment: deploy the SDK's whole
# runtime folder next to the exe. That covers Raster.dll *and* the OpenCV
# runtime the SDK ships alongside it -- exactly what an integrator does when
# they drop the SDK into their application folder.
add_custom_command(TARGET consumer_c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_raster_runtime}"
        "$<TARGET_FILE_DIR:consumer_c>"
)
# Windows 的运行时在 bin/（.dll），Unix 的在 lib/（.so / .dylib）。
file(GLOB _raster_runtime_dlls
    "${RASTER_SDK_ROOT}/bin/*.dll"
    "${RASTER_SDK_ROOT}/lib/*.so*"
    "${RASTER_SDK_ROOT}/lib/*.dylib")
foreach(_dll IN LISTS _raster_runtime_dlls)
    add_custom_command(TARGET consumer_c POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:consumer_c>"
    )
endforeach()
