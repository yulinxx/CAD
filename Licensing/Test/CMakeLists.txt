find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)

apr_add_test(NAME LicenseTests
    SOURCES
        LicenseTests.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../License/Src/KeygenTool.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../License/Src/LicenseKeygenDLL.cpp"
    LIBS License OpenSSL::SSL OpenSSL::Crypto
    LABELS license
    TIMEOUT 120
    WORKING_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    EXTRA_COMPILE_DEFS LICENSE_TEST_HOOKS LICENSE_KEYGEN_EXPORTS)

# 被测实现直接编入二进制，License 库头不在当前目录层级
target_include_directories(LicenseTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../License/Include")

message(STATUS "[License Tests]")
message(STATUS "  Target: LicenseTests")
message(STATUS "----------------------------------------")
