cmake_minimum_required(VERSION 3.20)

function(deploy_qt_libraries target_name qt_major_version)
    get_target_property(QT_QMAKE_EXECUTABLE Qt${qt_major_version}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_EXECUTABLE ${QT_QMAKE_EXECUTABLE} PATH)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")
    if(EXISTS ${QT_WINDEPLOYQT_EXECUTABLE})
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} --qmldir ${CMAKE_SOURCE_DIR} $<TARGET_FILE:${target_name}>
            COMMENT "Deploying Qt${qt_major_version} libraries using windeployqt"
        )
    else()
        message(FATAL_ERROR "windeployqt not found at ${QT_WINDEPLOYQT_EXECUTABLE}")
    endif()
endfunction()

file(GLOB_RECURSE MAIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp"
)

file(GLOB_RECURSE MAIN_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/*.h"
)

file(GLOB_RECURSE MAIN_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

set(app_name "CAD")

add_executable(${app_name}
    ${MAIN_SOURCES}
    ${MAIN_HEADERS}
    ${MAIN_RESOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${MAIN_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MAIN_SOURCES})

set(QT_COMPONENTS Core Gui Widgets)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS ${QT_COMPONENTS} REQUIRED)

set(QT_LIBRARIES)
foreach(comp ${QT_COMPONENTS})
    list(APPEND QT_LIBRARIES Qt${QT_VERSION_MAJOR}::${comp})
endforeach()

set_target_properties(${app_name} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    WIN32_EXECUTABLE ON
)

target_compile_features(${app_name} PRIVATE cxx_std_17)

target_include_directories(${app_name}
    PRIVATE
        "${CMAKE_SOURCE_DIR}/Core/Utility/Include"
        "${CMAKE_SOURCE_DIR}/Engine/Common/Include"
        "${CMAKE_SOURCE_DIR}/Engine/2D/Include"
        "${CMAKE_SOURCE_DIR}/Engine/3D/Include"
        "${CMAKE_SOURCE_DIR}/Render/Common/Include"
        "${CMAKE_SOURCE_DIR}/Render/2D/Include"
        "${CMAKE_SOURCE_DIR}/Render/3D/Include"
        "${CMAKE_SOURCE_DIR}/UI/Common/Include"
        "${CMAKE_SOURCE_DIR}/UI/2D/Include"
        "${CMAKE_SOURCE_DIR}/UI/3D/Include"
        "${CMAKE_SOURCE_DIR}/UI/App/Include"
        "${CMAKE_SOURCE_DIR}/Log/Log/Include"
)

target_link_libraries(${app_name}
    PRIVATE
        Utility
        EngineCommon
        Engine2D
        Engine3D
        RenderCommon
        Render2D
        Render3D
        UICommon
        UI2D
        UI3D
        UIApp
        Log
        ${QT_LIBRARIES}
)

if(MSVC)
    target_compile_options(${app_name} PRIVATE /utf-8)
endif()

# Qt 部署
deploy_qt_libraries(${app_name} ${QT_VERSION_MAJOR})

# 版本号
set(LIB_VERSION_MAJOR 1)
set(LIB_VERSION_MINOR 0)
set(LIB_VERSION_PATCH 0)
set(LIB_VERSION "${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH}")

set_target_properties(${app_name} PROPERTIES VERSION ${LIB_VERSION})

# 版本宏供源代码使用
string(TOUPPER ${app_name} _VER_PREFIX)
target_compile_definitions(${app_name} PUBLIC
    ${_VER_PREFIX}_VERSION_MAJOR=${LIB_VERSION_MAJOR}
    ${_VER_PREFIX}_VERSION_MINOR=${LIB_VERSION_MINOR}
    ${_VER_PREFIX}_VERSION_PATCH=${LIB_VERSION_PATCH}
    ${_VER_PREFIX}_VERSION_STRING="${LIB_VERSION}"
)

sanyi_add_version_info(${app_name} "${app_name}" "SanYi CAD Application")

message(STATUS "[Main]")
message(STATUS "  Application: ${app_name}")
message(STATUS "  Version: ${LIB_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")

message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "----------------------------------------")