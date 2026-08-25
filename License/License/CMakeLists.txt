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
if(NOT SANYI_ROOT)
    get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

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

# [B3-P0 修复] KeygenTool 和 LicenseKeygenDLL 包含私钥生成逻辑，
# 不应编译进发货 DLL。仅在 BUILD_KEYGEN_TOOL=ON 时编译为独立工具。
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/KeygenTool\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/LicenseKeygenDLL\\.cpp$")

# --- 第三方依赖 ---
find_package(OpenSSL REQUIRED)

# 统一入口已无条件生成调试符号（version.rc / PDB-DWARF），
# 该 option 仅为兼容旧构建参数保留，实际始终生效。
option(SANYI_LICENSE_SYMBOLS "Generate debug symbols for License" ON)

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
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Src"
    LINK_LIBRARIES
        PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
)

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

if(BUILD_LICENSE_TESTS)
    target_compile_definitions(${LIB_NAME} PRIVATE LICENSE_TEST_HOOKS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Test" "${CMAKE_CURRENT_BINARY_DIR}/LicenseTests")
    message(STATUS "  Tests: ENABLED")
else()
    message(STATUS "  Tests: DISABLED")
endif()

message(STATUS "----------------------------------------")

sanyi_export_imported_target(${LIB_NAME}
    LIB_NAME "${LIB_NAME}"
    PUBLIC_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    INTERFACE_LINK_LIBRARIES
)