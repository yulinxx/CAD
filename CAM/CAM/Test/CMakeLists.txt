# ============================================================================
# CAM 模块单元测试
# ============================================================================
# 测试目标只 link CAM::CAM（公开库），不 link 任何内部头
# 无 GUI 依赖，可在 CI 中 headless 运行
# ============================================================================

find_package(GTest REQUIRED)

add_executable(CAMTests
    CamCoreTests.cpp
)

target_link_libraries(CAMTests
    PRIVATE
        CAM
        GTest::gtest
        GTest::gtest_main
)

# 测试目标不需要 AUTOMOC（CAM 是纯 C++，无 Q_OBJECT）

add_test(NAME CAMTests COMMAND CAMTests)
