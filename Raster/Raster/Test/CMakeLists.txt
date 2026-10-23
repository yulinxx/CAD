# Raster 模块单元测试
#
# 直接把 Src 编进测试目标（而非链接 Raster.dll）：
#   - 可以白盒测试内部单元；
#   - 不必在 Windows 上处理 DLL 拷贝与运行时搜索路径；
#   - 定义 RASTER_STATIC 让导出宏退化为空，避免 dllimport 误判。
#
# 同时编一个**纯 C** 目标，证明公开头确实是纯 C 头：
# 第三方用 ctypes / cgo / P-Invoke 消费时，头里的任何 C++ 构造都会直接编译失败。

# C 语言必须由**顶层** CMakeLists 启用（CMake 官方要求 enable_language 在最高层
# 目录调用才生效）。这里读全局已启用语言列表来决定要不要跑 C 兼容性检查。
get_property(_raster_enabled_languages GLOBAL PROPERTY ENABLED_LANGUAGES)

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)
find_package(OpenCV REQUIRED)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../Src/*.cpp")

add_executable(RasterTests ${TEST_SOURCES} ${MODULE_SOURCES})

target_include_directories(RasterTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src"
)

target_compile_definitions(RasterTests PRIVATE RASTER_STATIC NOMINMAX)

target_link_libraries(RasterTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${OpenCV_LIBS}
)

target_compile_features(RasterTests PRIVATE cxx_std_17)

# 各测试文件在匿名 namespace 内可能定义同名辅助符号，
# unity build 会把多个 .cpp 合进同一 TU 导致 redefinition 错误。
set_property(TARGET RasterTests PROPERTY UNITY_BUILD OFF)

if(MSVC)
    target_compile_options(RasterTests PRIVATE /utf-8 /FS)
endif()

set_target_properties(RasterTests PROPERTIES FOLDER "Tests")

gtest_discover_tests(RasterTests
    DISCOVERY_TIMEOUT 60
)

# --- 纯 C 兼容性检查 -------------------------------------------------------
# 同样把 Src 编进来（而不是链接 DLL），这样测试不依赖 DLL 的运行时搜索路径。
if("C" IN_LIST _raster_enabled_languages)
    add_executable(RasterCApiCompatTest c_api_compat.c ${MODULE_SOURCES})
    target_include_directories(RasterCApiCompatTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Src"
    )
    target_compile_definitions(RasterCApiCompatTest PRIVATE RASTER_STATIC NOMINMAX)
    target_link_libraries(RasterCApiCompatTest PRIVATE ${OpenCV_LIBS})
    set_target_properties(RasterCApiCompatTest PROPERTIES FOLDER "Tests")
    set_property(TARGET RasterCApiCompatTest PROPERTY UNITY_BUILD OFF)
    if(MSVC)
        target_compile_options(RasterCApiCompatTest PRIVATE /utf-8)
    endif()
    add_test(NAME RasterCApiCompatTest COMMAND RasterCApiCompatTest)
else()
    message(STATUS "  [Raster] C language not enabled: skipping the C API compatibility check")
endif()

message(STATUS "  [Raster] RasterTests: enabled")
