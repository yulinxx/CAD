find_package(GTest REQUIRED)
find_package(OpenSSL REQUIRED)
include(GoogleTest)

add_executable(LicenseTests
    LicenseTests.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../License/Src/KeygenTool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../License/Src/LicenseKeygenDLL.cpp"
)

target_compile_features(LicenseTests PRIVATE cxx_std_17)

target_compile_definitions(LicenseTests PRIVATE LICENSE_TEST_HOOKS)

target_include_directories(LicenseTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../License/Include"
)

target_link_libraries(LicenseTests PRIVATE
    License
    OpenSSL::SSL
    OpenSSL::Crypto
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(LicenseTests)

set_property(TARGET LicenseTests PROPERTY FOLDER "Tests")

if(MSVC)
    target_compile_options(LicenseTests PRIVATE /utf-8)
endif()

message(STATUS "[License Tests]")
message(STATUS "  Target: LicenseTests")
message(STATUS "----------------------------------------")
