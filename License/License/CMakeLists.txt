cmake_minimum_required(VERSION 4.3)

set(LIB_NAME "License")
set(LIB_FOLDER "Core")
set(LIB_DESCRIPTION "License Verification Library")
set(LIB_EXPORT_MACRO "LICENSE_EXPORTS")

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

if(NOT SANYI_ROOT)
    get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

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

find_package(OpenSSL REQUIRED)

add_library(${LIB_NAME} SHARED
    ${LIB_SOURCES}
    ${LIB_HEADERS}
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(${LIB_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Src"
)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)
target_compile_definitions(${LIB_NAME} PRIVATE ${LIB_EXPORT_MACRO})

set_target_properties(${LIB_NAME} PROPERTIES
    FOLDER "${LIB_FOLDER}"
    DEBUG_POSTFIX "_d"
)

if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /utf-8 /FS)
else()
    target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(${LIB_NAME}
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

sanyi_add_version_info(${LIB_NAME} "${LIB_NAME}" "${LIB_DESCRIPTION}")

option(SANYI_LICENSE_SYMBOLS "Generate debug symbols for License" ON)
if(SANYI_LICENSE_SYMBOLS)
    sanyi_add_debug_symbols(${LIB_NAME})
endif()

message(STATUS "[${LIB_NAME}]")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")

if(BUILD_LICENSE_TESTS)
    target_compile_definitions(${LIB_NAME} PRIVATE LICENSE_TEST_HOOKS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Test" "${CMAKE_CURRENT_BINARY_DIR}/LicenseTests")
    message(STATUS "  Tests: ENABLED")
else()
    message(STATUS "  Tests: DISABLED")
endif()
