# 同一输入双实现对照工具（P3 A/B 基线）
# 产物：AprOffsetCompare（命令行，无 Qt GUI 依赖）
# 用法：
#   cmake --build build --target AprOffsetCompare
#   ./build/bin_Qt6/Release/AprOffsetCompare Docs/performance-audit/offset-ab-baseline.csv

option(BUILD_OFFSET_COMPARE "Build the 2D offset A/B comparison tool" ON)
if(NOT BUILD_OFFSET_COMPARE OR NOT TARGET Engraving)
    return()
endif()

add_executable(AprOffsetCompare main.cpp)

target_compile_features(AprOffsetCompare PRIVATE cxx_std_17)

target_include_directories(AprOffsetCompare PRIVATE
    "${APR_ROOT}/Engine/2D/Include"
    "${APR_ROOT}/Engine/Common/Include"
    "${APR_ROOT}/Engraving/Engraving/Include"
    "${APR_ROOT}/Utility/Utility/Include"
    "${APR_ROOT}/Log/Log/Include"
)

target_link_libraries(AprOffsetCompare PRIVATE
    Engine2D
    Engraving
    EngineCommon
    Utility
    Log
)

if(MSVC)
    target_compile_options(AprOffsetCompare PRIVATE /utf-8 /FS)
endif()

set_property(TARGET AprOffsetCompare PROPERTY FOLDER "Tools")
