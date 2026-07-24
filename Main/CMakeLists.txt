cmake_minimum_required(VERSION 4.3)

function(deploy_qt_libraries target_name qt_major_version)
    get_target_property(QT_QMAKE_EXECUTABLE Qt${qt_major_version}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_EXECUTABLE ${QT_QMAKE_EXECUTABLE} PATH)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")
    if(EXISTS ${QT_WINDEPLOYQT_EXECUTABLE})
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/.. $<TARGET_FILE:${target_name}>
            COMMENT "Deploying Qt${qt_major_version} libraries using windeployqt"
        )
    else()
        message(FATAL_ERROR "windeployqt not found at ${QT_WINDEPLOYQT_EXECUTABLE}")
    endif()
endfunction()

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/Src)
set(UI_ROOT ${SRC_ROOT}/UI)

file(GLOB_RECURSE MAIN_SOURCES CONFIGURE_DEPENDS
    "${SRC_ROOT}/**/*.cpp"
)

list(APPEND MAIN_SOURCES
    "${UI_ROOT}/ViewportSelector.cpp"
)

file(GLOB_RECURSE MAIN_HEADERS CONFIGURE_DEPENDS
    "${SRC_ROOT}/**/*.h"
)

list(REMOVE_DUPLICATES MAIN_SOURCES)
list(REMOVE_DUPLICATES MAIN_HEADERS)

list(FILTER MAIN_SOURCES EXCLUDE REGEX ".*Test/.*\\.cpp$")
list(FILTER MAIN_HEADERS EXCLUDE REGEX ".*Test/.*\\.h$")
if(NOT BUILD_UI3D)
    list(FILTER MAIN_SOURCES EXCLUDE REGEX ".*SimpleRenderer3D\\.cpp$")
    list(FILTER MAIN_HEADERS EXCLUDE REGEX ".*SimpleRenderer3D\\.h$")
    list(FILTER MAIN_SOURCES EXCLUDE REGEX ".*UiViewport3D\\.cpp$")
    list(FILTER MAIN_HEADERS EXCLUDE REGEX ".*UiViewport3D\\.h$")
    list(FILTER MAIN_SOURCES EXCLUDE REGEX ".*RenderWidget3DAdapter\\.cpp$")
    list(FILTER MAIN_HEADERS EXCLUDE REGEX ".*RenderWidget3DAdapter\\.h$")
endif()

file(GLOB_RECURSE MAIN_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

set(app_name "SanYiCAD")

set(APP_ORGANIZATION_NAME "SanYi")
set(APP_ORGANIZATION_DOMAIN "sanyi-cad.com")

configure_file(
    "${SRC_ROOT}/Common/VersionInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h"
    @ONLY
)

add_executable(${app_name}
    ${SRC_ROOT}/main.cpp
    ${MAIN_SOURCES}
    ${MAIN_HEADERS}
    ${MAIN_RESOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${MAIN_SOURCES} ${MAIN_HEADERS})

set(QT_COMPONENTS Core Gui Widgets OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS ${QT_COMPONENTS} REQUIRED)
set(QT_LIBRARIES)
foreach(comp ${QT_COMPONENTS})
    list(APPEND QT_LIBRARIES Qt${QT_VERSION_MAJOR}::${comp})
endforeach()

set_target_properties(${app_name} PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC ON
    WIN32_EXECUTABLE ON
    CXX_VISIBILITY_PRESET default
)

target_compile_features(${app_name} PRIVATE cxx_std_17)

target_include_directories(${app_name}
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${SRC_ROOT}"
        "${SRC_ROOT}/Common"
        "${SANYI_RENDERX_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/RenderCompat/Include"
        "${UI_ROOT}"
        "${UI_ROOT}/Adapters"
        "${UI_ROOT}/Dispatchers"
        "${UI_ROOT}/Documents"
        "${UI_ROOT}/Entities"
        "${UI_ROOT}/Render"
        "${UI_ROOT}/Services"
        "${UI_ROOT}/Widgets"
        "${UI_ROOT}/Workbench"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Utility/Utility/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../CrashHandler/CrashHandler/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../License/License/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Common/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/2D/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/3D/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Persistence/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/Common/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/2D/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/2D/Src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/3D/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Log/Log/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Include"
)

target_link_libraries(${app_name}
    PRIVATE
        Utility
        CrashHandler
        License
        EngineCommon
        Engine2D
        EnginePersistence
        SanYiRender
        UICommon
        UI2D
        Log
        RenderCompat
        FileIO
        ${QT_LIBRARIES}
)

if(BUILD_UI3D)
    target_link_libraries(${app_name} PRIVATE UI3D Engine3D)
    target_compile_definitions(${app_name} PRIVATE BUILD_UI3D=1)
endif()

if(SANYI_PYTHON_HOST)
    target_compile_definitions(${app_name} PRIVATE SANYI_PYTHON_HOST=1)
    target_compile_definitions(${app_name} PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/..\"")
    target_include_directories(${app_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../PythonHost/PythonHost/Include"
    )
    target_link_libraries(${app_name} PRIVATE PythonHost)
    add_dependencies(${app_name} PythonHost)

    add_custom_command(TARGET ${app_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/../Python"
            "$<TARGET_FILE_DIR:${app_name}>/Python"
        COMMENT "Deploying Python package to output directory"
    )
endif()

if(SANYI_PYBIND_CORE AND SANYI_PYTHON_HOST)
    add_dependencies(${app_name} SanYiPyBindCore)
endif()

if(MSVC)
    target_compile_options(${app_name} PRIVATE /utf-8 /FS)
endif()

set(TRANSLATIONS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../UI/Common/Translations")
set(TRANSLATIONS_DST_DIR "$<TARGET_FILE_DIR:${app_name}>/translations")

if(TARGET UICommon_translations)
    add_dependencies(${app_name} UICommon_translations)
endif()

file(GLOB TRANSLATION_QM_FILES "${TRANSLATIONS_SRC_DIR}/*.qm")

if(TRANSLATION_QM_FILES)
    add_custom_command(TARGET ${app_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSLATIONS_DST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${TRANSLATION_QM_FILES}
            "${TRANSLATIONS_DST_DIR}/"
        COMMENT "Copying translation .qm files to ${TRANSLATIONS_DST_DIR}"
    )
endif()

set(RESOURCES_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(RESOURCES_DST_DIR "$<TARGET_FILE_DIR:${app_name}>/resources")

add_custom_command(TARGET ${app_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RESOURCES_SRC_DIR}"
        "${RESOURCES_DST_DIR}"
    COMMENT "Copying resource files to ${RESOURCES_DST_DIR}"
)

deploy_qt_libraries(${app_name} ${QT_VERSION_MAJOR})

sanyi_add_version_info(${app_name} "${app_name}" "SanYi CAD Application")

option(SANYI_GENERATE_SYMBOLS "Generate debug symbols for crash analysis" ON)
if(SANYI_GENERATE_SYMBOLS)
    sanyi_add_debug_symbols(${app_name})
endif()

message(STATUS "[Main]")
message(STATUS "  Application: ${app_name}")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "----------------------------------------")

option(BUILD_MAIN_TESTS "Build Main module unit tests" ON)
if(BUILD_MAIN_TESTS AND GTest_FOUND)
    file(GLOB MAIN_TEST_SOURCES CONFIGURE_DEPENDS
        "${UI_ROOT}/Test/*.cpp"
    )

    if(NOT BUILD_UI3D)
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*SimpleRenderer3DTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*FrameworkLifecycleTests\\.cpp$")
    endif()

    if(NOT BUILD_RENDERCORE)
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*RenderCoreTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*BackendE2ETests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*FrameworkIntegrationTests\\.cpp$")
    endif()

    file(GLOB MAIN_TEST_DEP_SOURCES CONFIGURE_DEPENDS
        "${UI_ROOT}/*.cpp"
        "${UI_ROOT}/**/*.cpp"
        "${SRC_ROOT}/Persistence/*.cpp"
        "${SRC_ROOT}/Persistence/Repositories/*.cpp"
        "${SRC_ROOT}/Import/*.cpp"
    )

    if(NOT BUILD_UI3D)
        list(FILTER MAIN_TEST_DEP_SOURCES EXCLUDE REGEX ".*SimpleRenderer3D\\.cpp$")
        list(FILTER MAIN_TEST_DEP_SOURCES EXCLUDE REGEX ".*UiViewport3D\\.cpp$")
        list(FILTER MAIN_TEST_DEP_SOURCES EXCLUDE REGEX ".*RenderWidget3DAdapter\\.cpp$")
    endif()

    add_executable(MainTests
        ${MAIN_TEST_SOURCES}
        ${MAIN_TEST_DEP_SOURCES}
    )

    target_compile_features(MainTests PRIVATE cxx_std_17)

    target_include_directories(MainTests
        PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${SRC_ROOT}"
            "${SRC_ROOT}/Common"
            "${SRC_ROOT}/Persistence"
            "${SRC_ROOT}/Persistence/Repositories"
            "${SRC_ROOT}/Import"
            "${UI_ROOT}"
            "${UI_ROOT}/Adapters"
            "${UI_ROOT}/Dispatchers"
            "${UI_ROOT}/Documents"
            "${UI_ROOT}/Entities"
            "${UI_ROOT}/Render"
            "${UI_ROOT}/Services"
            "${UI_ROOT}/Widgets"
            "${UI_ROOT}/Workbench"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Utility/Utility/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../CrashHandler/CrashHandler/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../License/License/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Common/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/2D/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/3D/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Persistence/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../UI/Common/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../UI/2D/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../UI/3D/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Log/Log/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Include"
    )

    target_link_libraries(MainTests
        PRIVATE
            Utility
            EngineCommon
            Engine2D
            EnginePersistence
            RenderCompat
            UICommon
            UI2D
            Log
            SanYiRender
            FileIO
            ${QT_LIBRARIES}
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
    )

    if(BUILD_UI3D)
        target_link_libraries(MainTests PRIVATE Engine3D UI3D)
        target_compile_definitions(MainTests PRIVATE BUILD_UI3D=1)
    endif()

    set_target_properties(MainTests PROPERTIES
        AUTOMOC ON
        AUTOUIC OFF
    )

    deploy_qt_libraries(MainTests ${QT_VERSION_MAJOR})

    if(MSVC)
        set_target_properties(MainTests PROPERTIES
            LINK_FLAGS "/OPT:NOREF /OPT:NOICF"
        )
        target_compile_options(MainTests PRIVATE /utf-8 /FS)
    endif()

    gtest_discover_tests(MainTests)

    message(STATUS "[Main Tests]")
    # message(STATUS "  SimpleRenderer3DTests: Renderer lifecycle, render loop, selection")
    # message(STATUS "  CommandLifecycleTests: execute/cancel/submit, undo/redo, preview, selection, handlerFor")
    # (RenderCoreTests/BackendE2ETests/FrameworkIntegrationTests 已删除，依赖 RenderCore 且无实装)
    # message(STATUS "  RenderPipelineTests: SceneEditServiceAdapter commit/preview/cancel + scene-changed signal/callback")
    # message(STATUS "  GpuSceneRenderer2DTests: GPU 桥接器编译/投递/选择/overlay 路径 + Main→Render2D 格式翻译")
    message(STATUS "")
endif()