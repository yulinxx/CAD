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

set(MAIN_APP_SOURCES
    ${SRC_ROOT}/main.cpp
    ${SRC_ROOT}/Common/AppInitializer.cpp
    ${SRC_ROOT}/Common/AppPathManager.cpp
    ${SRC_ROOT}/Common/CrashHandlerBootstrap.cpp
    ${SRC_ROOT}/Bootstrap/AppBootstrapper.cpp
    ${SRC_ROOT}/Composition/ApplicationCompositionRoot.cpp
    ${SRC_ROOT}/Composition/CoreOperationRegistry.cpp
    ${SRC_ROOT}/Composition/DocumentPersistenceHelper.cpp
    ${SRC_ROOT}/Composition/FileOperationRegistry.cpp
    ${SRC_ROOT}/Composition/PendingOperationRegistry.cpp
    ${SRC_ROOT}/Export/Adapters/DocumentExportAdapter.cpp
    ${SRC_ROOT}/Export/Adapters/MetadataExportAdapter.cpp
    ${SRC_ROOT}/Export/Adapters/ViewportExportAdapter.cpp
    ${SRC_ROOT}/Export/ExportDispatcher.cpp
    ${SRC_ROOT}/Export/ExportService.cpp
    ${SRC_ROOT}/Export/Writers/BmpExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/DxfExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/ObjExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/PdfExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/PngExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/StepExportWriter.cpp
    ${SRC_ROOT}/Export/Writers/SvgExportWriter.cpp
    ${SRC_ROOT}/Import/Adapters/DocumentImportAdapter.cpp
    ${SRC_ROOT}/Import/Adapters/PropertyImportAdapter.cpp
    ${SRC_ROOT}/Import/Adapters/TreeImportAdapter.cpp
    ${SRC_ROOT}/Import/Adapters/ViewportImportAdapter.cpp
    ${SRC_ROOT}/Import/FioEntityConverter.cpp
    ${SRC_ROOT}/Import/ImportDispatcher.cpp
    ${SRC_ROOT}/Import/ImportService.cpp
    ${SRC_ROOT}/Import/Readers/DxfImportReader.cpp
    ${SRC_ROOT}/Import/Readers/ObjImportReader.cpp
    ${SRC_ROOT}/Import/Readers/PdfImportReader.cpp
    ${SRC_ROOT}/Import/Readers/PltImportReader.cpp
    ${SRC_ROOT}/Import/Readers/StepImportReader.cpp
    ${SRC_ROOT}/Import/Readers/StlImportReader.cpp
    ${SRC_ROOT}/Import/Readers/SvgImportReader.cpp
    ${SRC_ROOT}/License/LicenseDialog.cpp
    ${SRC_ROOT}/Persistence/DatabaseBootstrapper.cpp
    ${SRC_ROOT}/Persistence/LayerPersistenceBridge.cpp
    ${SRC_ROOT}/Persistence/PersistenceService.cpp
    ${SRC_ROOT}/Persistence/Repositories/DocumentRepository.cpp
    ${SRC_ROOT}/Persistence/Repositories/LayerRepository.cpp
    ${SRC_ROOT}/Persistence/Repositories/RecentFileRepository.cpp
    ${SRC_ROOT}/Persistence/Repositories/SettingsRepository.cpp
    ${SRC_ROOT}/Persistence/Repositories/WorkspaceSnapshotRepository.cpp
    ${SRC_ROOT}/Runtime/CADApplicationEntry.cpp
    ${SRC_ROOT}/Runtime/CADApplicationRuntime.cpp
    ${SRC_ROOT}/UI/Adapters/TransformDialogAdapter.cpp
    ${SRC_ROOT}/UI/Documents/SceneBuilder3D.cpp
    ${SRC_ROOT}/UI/Documents/SceneDocument2D.cpp
    ${SRC_ROOT}/UI/Entities/UiEntities.cpp
    ${SRC_ROOT}/UI/Entities/UiGeometryAlgorithms.cpp
    ${SRC_ROOT}/UI/Interaction/UiInteractionDispatcher.cpp
    ${SRC_ROOT}/UI/Render/Camera2D.cpp
    ${SRC_ROOT}/UI/Render/EntityToVertices.cpp
    ${SRC_ROOT}/UI/Render/RenderViewport2D.cpp
    ${SRC_ROOT}/UI/Render/Renderer3DFactory.cpp
    ${SRC_ROOT}/UI/Render/SceneRefreshCoordinator.cpp
    ${SRC_ROOT}/UI/Render/ViewportInputRouter.cpp
    ${SRC_ROOT}/UI/Services/FileDialogService.cpp
    ${SRC_ROOT}/UI/Services/HelpDialogService.cpp
    ${SRC_ROOT}/UI/Services/RecentFileService.cpp
    ${SRC_ROOT}/UI/Services/SelectionService.cpp
    ${SRC_ROOT}/UI/Services/UiLayoutService.cpp
    ${SRC_ROOT}/UI/Services/UiShellHost.cpp
    ${SRC_ROOT}/UI/Services/UiStateCenter.cpp
    ${SRC_ROOT}/UI/Services/UiThemeService.cpp
    ${SRC_ROOT}/UI/ClientConfig/UiConfigLoader.cpp
    ${SRC_ROOT}/UI/ClientConfig/UiConfigurationManager.cpp
    ${SRC_ROOT}/UI/ClientConfig/UiLayoutBuilder.cpp
    ${SRC_ROOT}/UI/ClientConfig/UiPanelRegistry.cpp
    ${SRC_ROOT}/UI/Workbench/UiWorkbench.cpp
    ${SRC_ROOT}/UI/Workbench/DrawToolSwitchRegistry.cpp
    ${SRC_ROOT}/UI/Workbench/WorkbenchActionManager.cpp
    ${SRC_ROOT}/UI/Workbench/WorkbenchLayoutManager.cpp
    ${SRC_ROOT}/UI/Workbench/WorkbenchMenuManager.cpp
    ${SRC_ROOT}/UI/Workbench/WorkbenchStateManager.cpp
    ${SRC_ROOT}/UI/Workbench/WorkbenchWindow.cpp
    ${SRC_ROOT}/UI/Widgets/DrawToolBarWidget.cpp
    ${SRC_ROOT}/UI/Widgets/UiPropertiesPanel.cpp
    ${SRC_ROOT}/UI/Widgets/UiSceneTreeDock.cpp
    ${SRC_ROOT}/UI/Widgets/ViewportSelector.cpp
)

if(BUILD_UI3D)
    list(APPEND MAIN_APP_SOURCES
        ${SRC_ROOT}/UI/Render/RenderWidget3DAdapter.cpp
        ${SRC_ROOT}/UI/Render/SimpleRenderer3D.cpp
        ${SRC_ROOT}/UI/Render/UiViewport3D.cpp
    )
endif()

set(MAIN_APP_HEADERS
    ${SRC_ROOT}/Common/VersionInfo.h.in
)

file(GLOB_RECURSE MAIN_RESOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

set(app_name "SanYiCAD")

set(APP_ORGANIZATION_NAME "SanYi")
set(APP_ORGANIZATION_DOMAIN "sanyi-cad.com")

# CMake 4.3 configure_file 兼容性：改用 file(READ) + string(CONFIGURE) + file(WRITE)
file(READ "${SRC_ROOT}/Common/VersionInfo.h.in" _version_info_content)
string(CONFIGURE "${_version_info_content}" _version_info_configured @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h" "${_version_info_configured}")

add_executable(${app_name}
    ${MAIN_APP_SOURCES}
    ${MAIN_APP_HEADERS}
    ${MAIN_RESOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${MAIN_APP_SOURCES} ${MAIN_APP_HEADERS})

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
        "${UI_ROOT}"
        "${UI_ROOT}/Adapters"
        "${UI_ROOT}/Interaction"
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
        "${CMAKE_CURRENT_SOURCE_DIR}/../UI/3D/Src"
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
        FileIO
        ${QT_LIBRARIES}
)

# 客户化 UI 配置
target_compile_definitions(${app_name} PRIVATE SANYI_CLIENT_ID="${SANYI_CLIENT_ID}")
if(SANYI_ENABLE_CONFIG_DRIVEN_UI)
    target_compile_definitions(${app_name} PRIVATE SANYI_ENABLE_CONFIG_DRIVEN_UI=1)
endif()

if(BUILD_UI3D)
    target_link_libraries(${app_name} PRIVATE UI3D Engine3D)
    target_compile_definitions(${app_name} PRIVATE BUILD_UI3D=1)
    # 显式传播 ENABLE_GEOMODELCORE：SceneEditService3D / ServicePack3D 等导出类
    # 含 #ifdef ENABLE_GEOMODELCORE 条件数据成员，EXE 必须与 DLL 的 sizeof 一致
    if(BUILD_GEOMODELCORE)
        target_compile_definitions(${app_name} PRIVATE ENABLE_GEOMODELCORE)
    endif()
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

# ============ 部署第三方外部工具 (poppler / ghostscript) ============
# 从 ThirdParty/<tool>/<platform>/<arch>/ 下的 bin/ 和 lib/ 复制到输出目录
# 支持格式:
#   Windows: ThirdParty/poppler/windows/x64/bin/
#   Linux:   ThirdParty/poppler/linux/x86_64/{bin,lib}/
#   macOS:   ThirdParty/poppler/macos/arm64/{bin,lib}/ 或 macos/x86_64/
#
# 工作原理:
#   - Windows: dll 和 exe 都在 bin/，直接全部复制到输出目录
#   - Linux/macOS: bin/ 放可执行文件，lib/ 放依赖库；
#                  调用 PdfToSvgConverter 运行时会自动设置 LD_LIBRARY_PATH / DYLD_LIBRARY_PATH
set(SANYI_THIRDPARTY_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty")

if(WIN32)
    set(_tools_platform "windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_tools_arch "x64")
    else()
        set(_tools_arch "x86")
    endif()
elseif(APPLE)
    set(_tools_platform "macos")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_tools_arch "arm64")
    else()
        set(_tools_arch "x86_64")
    endif()
elseif(UNIX)
    set(_tools_platform "linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(_tools_arch "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_tools_arch "arm64")
    else()
        set(_tools_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

# 要部署的工具列表（按优先级/依赖顺序）
set(_deploy_tools "poppler" "ghostscript")

foreach(_tool ${_deploy_tools})
    set(_tool_root_dir "${SANYI_THIRDPARTY_TOOLS_DIR}/${_tool}/${_tools_platform}/${_tools_arch}")
    set(_tool_bin_dir "${_tool_root_dir}/bin")
    set(_tool_lib_dir "${_tool_root_dir}/lib")

    set(_tool_deployed FALSE)

    # 部署 bin/ 目录
    if(EXISTS "${_tool_bin_dir}")
        file(GLOB _tool_bin_files "${_tool_bin_dir}/*")
        list(LENGTH _tool_bin_files _tool_bin_file_count)
        if(_tool_bin_file_count GREATER 0)
            # Windows: 直接复制到输出目录（dll + exe 在一起）
            if(WIN32)
                add_custom_command(TARGET ${app_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_tool_bin_dir}"
                        "$<TARGET_FILE_DIR:${app_name}>"
                    COMMENT "Deploying ${_tool} binaries to output directory"
                )
            else()
                # Linux/macOS: 复制 bin/ 到输出目录的 bin/ 子目录，保持结构
                add_custom_command(TARGET ${app_name} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app_name}>/tools/${_tool}/bin"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${_tool_bin_dir}"
                        "$<TARGET_FILE_DIR:${app_name}>/tools/${_tool}/bin"
                    COMMENT "Deploying ${_tool} bin/ to tools/${_tool}/bin"
                )
            endif()
            set(_tool_deployed TRUE)
            message(STATUS "  [Main] ${_tool} bin: ${_tool_bin_dir} (${_tool_bin_file_count} files)")
        else()
            message(STATUS "  [Main] ${_tool}: bin/ exists but empty (${_tool_bin_dir})")
        endif()
    else()
        message(STATUS "  [Main] ${_tool}: no bin/ directory at ${_tool_bin_dir}")
    endif()

    # 部署 lib/ 目录（仅 Linux/macOS）
    if(NOT WIN32 AND EXISTS "${_tool_lib_dir}")
        file(GLOB _tool_lib_files "${_tool_lib_dir}/*.so*" "${_tool_lib_dir}/*.dylib")
        list(LENGTH _tool_lib_files _tool_lib_file_count)
        if(_tool_lib_file_count GREATER 0)
            add_custom_command(TARGET ${app_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app_name}>/tools/${_tool}/lib"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_tool_lib_dir}"
                    "$<TARGET_FILE_DIR:${app_name}>/tools/${_tool}/lib"
                COMMENT "Deploying ${_tool} lib/ to tools/${_tool}/lib"
            )
            set(_tool_deployed TRUE)
            message(STATUS "  [Main] ${_tool} lib: ${_tool_lib_dir} (${_tool_lib_file_count} files)")
        endif()
    endif()

    if(NOT _tool_deployed)
        message(STATUS "  [Main] ${_tool}: nothing to deploy (will use system PATH if available)")
    endif()
endforeach()

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
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*RenderWidget3DAdapterTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*Scene3DRegressionTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*FrameworkLifecycleTests\\.cpp$")
    endif()

    set(MAIN_TEST_DEP_SOURCES
        ${SRC_ROOT}/Common/AppInitializer.cpp
        ${SRC_ROOT}/Common/AppPathManager.cpp
        ${SRC_ROOT}/Common/CrashHandlerBootstrap.cpp
        ${SRC_ROOT}/Composition/ApplicationCompositionRoot.cpp
        ${SRC_ROOT}/Composition/CoreOperationRegistry.cpp
        ${SRC_ROOT}/Composition/DocumentPersistenceHelper.cpp
        ${SRC_ROOT}/Composition/FileOperationRegistry.cpp
        ${SRC_ROOT}/Composition/PendingOperationRegistry.cpp
        ${SRC_ROOT}/Export/Adapters/DocumentExportAdapter.cpp
        ${SRC_ROOT}/Export/Adapters/MetadataExportAdapter.cpp
        ${SRC_ROOT}/Export/Adapters/ViewportExportAdapter.cpp
        ${SRC_ROOT}/Export/ExportDispatcher.cpp
        ${SRC_ROOT}/Export/ExportService.cpp
        ${SRC_ROOT}/Export/Writers/BmpExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/DxfExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/ObjExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/PdfExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/PngExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/StepExportWriter.cpp
        ${SRC_ROOT}/Export/Writers/SvgExportWriter.cpp
        ${SRC_ROOT}/Import/Adapters/DocumentImportAdapter.cpp
        ${SRC_ROOT}/Import/Adapters/PropertyImportAdapter.cpp
        ${SRC_ROOT}/Import/Adapters/TreeImportAdapter.cpp
        ${SRC_ROOT}/Import/Adapters/ViewportImportAdapter.cpp
        ${SRC_ROOT}/Import/FioEntityConverter.cpp
        ${SRC_ROOT}/Import/ImportDispatcher.cpp
        ${SRC_ROOT}/Import/ImportService.cpp
        ${SRC_ROOT}/Import/Readers/DxfImportReader.cpp
        ${SRC_ROOT}/Import/Readers/ObjImportReader.cpp
        ${SRC_ROOT}/Import/Readers/PdfImportReader.cpp
        ${SRC_ROOT}/Import/Readers/PltImportReader.cpp
        ${SRC_ROOT}/Import/Readers/StepImportReader.cpp
        ${SRC_ROOT}/Import/Readers/StlImportReader.cpp
        ${SRC_ROOT}/Import/Readers/SvgImportReader.cpp
        ${SRC_ROOT}/Persistence/DatabaseBootstrapper.cpp
        ${SRC_ROOT}/Persistence/LayerPersistenceBridge.cpp
        ${SRC_ROOT}/Persistence/PersistenceService.cpp
        ${SRC_ROOT}/Persistence/Repositories/DocumentRepository.cpp
        ${SRC_ROOT}/Persistence/Repositories/LayerRepository.cpp
        ${SRC_ROOT}/Persistence/Repositories/RecentFileRepository.cpp
        ${SRC_ROOT}/Persistence/Repositories/SettingsRepository.cpp
        ${SRC_ROOT}/Persistence/Repositories/WorkspaceSnapshotRepository.cpp
        ${SRC_ROOT}/UI/Adapters/TransformDialogAdapter.cpp
        ${SRC_ROOT}/UI/ClientConfig/UiConfigLoader.cpp
        ${SRC_ROOT}/UI/ClientConfig/UiConfigurationManager.cpp
        ${SRC_ROOT}/UI/ClientConfig/UiLayoutBuilder.cpp
        ${SRC_ROOT}/UI/ClientConfig/UiPanelRegistry.cpp
        ${SRC_ROOT}/UI/Documents/SceneDocument2D.cpp
        ${SRC_ROOT}/UI/Entities/UiEntities.cpp
        ${SRC_ROOT}/UI/Entities/UiGeometryAlgorithms.cpp
        ${SRC_ROOT}/UI/Interaction/UiInteractionDispatcher.cpp
        ${SRC_ROOT}/UI/Render/Camera2D.cpp
        ${SRC_ROOT}/UI/Render/EntityToVertices.cpp
        ${SRC_ROOT}/UI/Render/RenderViewport2D.cpp
        ${SRC_ROOT}/UI/Render/Renderer3DFactory.cpp
        ${SRC_ROOT}/UI/Render/SceneRefreshCoordinator.cpp
        ${SRC_ROOT}/UI/Render/ViewportInputRouter.cpp
        ${SRC_ROOT}/UI/Services/FileDialogService.cpp
        ${SRC_ROOT}/UI/Services/HelpDialogService.cpp
        ${SRC_ROOT}/UI/Services/RecentFileService.cpp
        ${SRC_ROOT}/UI/Services/SelectionService.cpp
        ${SRC_ROOT}/UI/Services/UiLayoutService.cpp
        ${SRC_ROOT}/UI/Services/UiShellHost.cpp
        ${SRC_ROOT}/UI/Services/UiStateCenter.cpp
        ${SRC_ROOT}/UI/Services/UiThemeService.cpp
        ${SRC_ROOT}/UI/Workbench/UiWorkbench.cpp
        ${SRC_ROOT}/UI/Workbench/DrawToolSwitchRegistry.cpp
        ${SRC_ROOT}/UI/Workbench/WorkbenchActionManager.cpp
        ${SRC_ROOT}/UI/Workbench/WorkbenchLayoutManager.cpp
        ${SRC_ROOT}/UI/Workbench/WorkbenchMenuManager.cpp
        ${SRC_ROOT}/UI/Workbench/WorkbenchStateManager.cpp
        ${SRC_ROOT}/UI/Workbench/WorkbenchWindow.cpp
        ${SRC_ROOT}/UI/Widgets/DrawToolBarWidget.cpp
        ${SRC_ROOT}/UI/Widgets/UiPropertiesPanel.cpp
        ${SRC_ROOT}/UI/Widgets/UiSceneTreeDock.cpp
        ${SRC_ROOT}/UI/Widgets/ViewportSelector.cpp
    )

    if(BUILD_UI3D)
        list(APPEND MAIN_TEST_DEP_SOURCES
            ${SRC_ROOT}/UI/Render/RenderWidget3DAdapter.cpp
            ${SRC_ROOT}/UI/Render/SimpleRenderer3D.cpp
            ${SRC_ROOT}/UI/Render/UiViewport3D.cpp
        )
    endif()

    # P2 测试扩展: 添加 protobuf 生成的源文件以支持 SyEntitySerializer 测试
    set(PROTO_GEN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Generated/Proto/SanYiDocument.pb.cc")

    # P5 测试扩展: 添加 BaseTool.cpp（非导出类，UI2D 库不导出其符号）
    set(BASE_TOOL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../UI/2D/Src/Ui/DrawTools/BaseTool.cpp")

    find_package(protobuf CONFIG REQUIRED)

    add_executable(MainTests
        ${MAIN_TEST_SOURCES}
        ${MAIN_TEST_DEP_SOURCES}
        ${PROTO_GEN_SRC}
        ${BASE_TOOL_SRC}
        ${SRC_ROOT}/UI/ClientConfig/configs.qrc
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
            "${SRC_ROOT}/Export"
            "${UI_ROOT}"
            "${UI_ROOT}/Adapters"
            "${UI_ROOT}/Interaction"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/../UI/3D/Src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Log/Log/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../FileIO/FileIO/Generated/Proto"
    )

    target_link_libraries(MainTests
        PRIVATE
            Utility
            CrashHandler
            EngineCommon
            Engine2D
            EnginePersistence
            UICommon
            UI2D
            Log
            SanYiRender
            FileIO
            protobuf::libprotobuf-lite
            ${QT_LIBRARIES}
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
    )

    if(BUILD_UI3D)
        target_link_libraries(MainTests PRIVATE Engine3D UI3D)
        target_compile_definitions(MainTests PRIVATE BUILD_UI3D=1)
        if(BUILD_GEOMODELCORE)
            target_compile_definitions(MainTests PRIVATE ENABLE_GEOMODELCORE)
        endif()
    endif()

    # 客户化 UI 配置（测试目标同样需要访问资源）
    target_compile_definitions(MainTests PRIVATE SANYI_CLIENT_ID="${SANYI_CLIENT_ID}")
    if(SANYI_ENABLE_CONFIG_DRIVEN_UI)
        target_compile_definitions(MainTests PRIVATE SANYI_ENABLE_CONFIG_DRIVEN_UI=1)
    endif()

    set_target_properties(MainTests PROPERTIES
        AUTOMOC ON
        AUTOUIC OFF
        AUTORCC ON
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
    message(STATUS "")
endif()