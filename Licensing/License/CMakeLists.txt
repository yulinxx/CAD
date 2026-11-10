cmake_minimum_required(VERSION 4.3)

# --- 模块元信息 ---
set(LIB_NAME "License")
set(LIB_FOLDER "Core")
set(LIB_DESCRIPTION "License Verification Library")
set(LIB_EXPORT_MACRO "LICENSE_EXPORTS")

# --- 版本（按模块独立）---
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# --- 路径 ---
if(NOT APR_ROOT)
    get_filename_component(APR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
include("${APR_ROOT}/CMake/AprPaths.cmake")
include("${APR_ROOT}/CMake/Utils.cmake")

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
list(FILTER LIB_SOURCES EXCLUDE REGEX "${APR_SOURCE_EXCLUDE_REGEX}")
list(FILTER LIB_HEADERS EXCLUDE REGEX "${APR_HEADER_EXCLUDE_REGEX}")

# KeygenTool 和 LicenseKeygenDLL 包含私钥生成逻辑，
# 不应编译进发货 DLL。仅在 BUILD_KEYGEN_TOOL=ON 时编译为独立工具。
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/KeygenTool\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/LicenseKeygenDLL\\.cpp$")

# --- 第三方依赖 ---
find_package(OpenSSL CONFIG REQUIRED)

# --- 统一入口创建 DLL ---
apr_add_shared_library(${LIB_NAME}
    FOLDER "${LIB_FOLDER}"
    DESCRIPTION "${LIB_DESCRIPTION}"
    EXPORT_MACRO "${LIB_EXPORT_MACRO}"
    VERSION_MAJOR ${VERSION_MAJOR}
    VERSION_MINOR ${VERSION_MINOR}
    VERSION_PATCH ${VERSION_PATCH}
    SOURCES ${LIB_SOURCES}
    HEADERS ${LIB_HEADERS}
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Src"
    PRIVATE_LINK_LIBRARIES
        OpenSSL::SSL
        OpenSSL::Crypto
        Log
)

# 许可校验开关必须编进本库（LicenseInternal.cpp 依据宏决定 g_checkEnabled 默认值），
# 只加在 Main 上不会生效。APR_ENABLE_LICENSE 在 Config.cmake 声明，默认 ON。
target_compile_definitions(${LIB_NAME} PRIVATE $<$<BOOL:${APR_ENABLE_LICENSE}>:APR_ENABLE_LICENSE>)

if(WIN32)
    target_link_libraries(${LIB_NAME} PRIVATE Iphlpapi.lib)
elseif(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation)
    if(IOKIT_FRAMEWORK)
        target_link_libraries(${LIB_NAME} PRIVATE ${IOKIT_FRAMEWORK})
    endif()
    if(CORE_FOUNDATION_FRAMEWORK)
        target_link_libraries(${LIB_NAME} PRIVATE ${CORE_FOUNDATION_FRAMEWORK})
    endif()
endif()

# 测试注入钩子：仅在构建测试时编入 License DLL，会额外导出测试专用接口。
# 出货构建必须 BUILD_TESTS=OFF，否则这些测试注入接口会一起进入发布产物。
apr_module_tests_enabled(LICENSE _license_tests)
if(_license_tests)
    target_compile_definitions(${LIB_NAME} PRIVATE LICENSE_TEST_HOOKS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Test" "${CMAKE_CURRENT_BINARY_DIR}/LicenseTests")
endif()

# --- 状态输出 ---
message(STATUS "[${LIB_NAME}]")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")

apr_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    INTERFACE_LINK_LIBRARIES
)