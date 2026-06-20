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

set(MAIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Common/AppInitializer.cpp"
)

set(MAIN_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/Common/AppInitializer.h"
)

file(GLOB_RECURSE MAIN_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

set(app_name "CAD")

# 版本号定义
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(PROJECT_VERSION_MAJOR ${VERSION_MAJOR})
set(PROJECT_VERSION_MINOR ${VERSION_MINOR})
set(PROJECT_VERSION_PATCH ${VERSION_PATCH})
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

add_executable(${app_name}
    ${MAIN_SOURCES}
    ${MAIN_HEADERS}
    ${MAIN_RESOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${MAIN_SOURCES} ${MAIN_HEADERS})

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
        "${CMAKE_SOURCE_DIR}/Utility/Utility/Include"
        "${CMAKE_SOURCE_DIR}/Engine/Common/Include"
        "${CMAKE_SOURCE_DIR}/Engine/2D/Include"
        "${CMAKE_SOURCE_DIR}/Engine/3D/Include"
        "${CMAKE_SOURCE_DIR}/Render/Common/Include"
        "${CMAKE_SOURCE_DIR}/Render/2D/Include"
        "${CMAKE_SOURCE_DIR}/Render/3D/Include"
        "${CMAKE_SOURCE_DIR}/UI/Common/Include"
        "${CMAKE_SOURCE_DIR}/UI/2D/Include"
        "${CMAKE_SOURCE_DIR}/UI/3D/Include"
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
        Log
        ${QT_LIBRARIES}
)

if(MSVC)
    target_compile_options(${app_name} PRIVATE /utf-8)
endif()

# 复制翻译文件 (.qm) 到输出目录
set(TRANSLATIONS_SRC_DIR "${CMAKE_SOURCE_DIR}/UI/Common/Translations")
set(TRANSLATIONS_DST_DIR "$<TARGET_FILE_DIR:${app_name}>/translations")

file(GLOB TRANSLATION_QM_FILES "${TRANSLATIONS_SRC_DIR}/*.qm")

add_custom_command(TARGET ${app_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSLATIONS_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TRANSLATION_QM_FILES}
        "${TRANSLATIONS_DST_DIR}/"
    COMMENT "Copying translation .qm files to ${TRANSLATIONS_DST_DIR}"
)

# 复制资源文件 (Shortcuts 等) 到输出目录
set(RESOURCES_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(RESOURCES_DST_DIR "$<TARGET_FILE_DIR:${app_name}>/resources")

add_custom_command(TARGET ${app_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RESOURCES_SRC_DIR}"
        "${RESOURCES_DST_DIR}"
    COMMENT "Copying resource files to ${RESOURCES_DST_DIR}"
)

# Qt 部署
deploy_qt_libraries(${app_name} ${QT_VERSION_MAJOR})

sanyi_add_version_info(${app_name} "${app_name}" "SanYi CAD Application")

message(STATUS "[Main]")
message(STATUS "  Application: ${app_name}")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "----------------------------------------")