cmake_minimum_required(VERSION 4.3)

function(deploy_qt_libraries target_name qt_major_version)
    if(NOT WIN32)
        return()
    endif()
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

# 自动收集所有源文件（新增 .cpp 文件无需修改此处）
file(GLOB_RECURSE MAIN_APP_SOURCES CONFIGURE_DEPENDS
    "${SRC_ROOT}/*.cpp"
)
# 排除测试目录下的文件（这些由测试目标独立编译，不属于应用可执行文件）
list(FILTER MAIN_APP_SOURCES EXCLUDE REGEX ".*/UI/Test/.*\.cpp$")

# macOS 平台专用 Objective-C++ 文件：上面的 glob 只收 .cpp，.mm 必须显式加入。
# 追加进 MAIN_APP_SOURCES（而非 add_executable 后 target_sources）是为了让
# MainTests 一并获得——它复用 MAIN_APP_SOURCES，否则 macOS 下测试目标会缺符号。
if(APPLE)
    list(APPEND MAIN_APP_SOURCES
        "${SRC_ROOT}/Platform/MacMenuCleanup.mm"
    )
endif()

set(MAIN_APP_HEADERS
    ${SRC_ROOT}/Common/VersionInfo.h.in
    ${SRC_ROOT}/UI/ClientConfig/ExampleCustomPanel.h
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

# ---------------------------------------------------------------------------
# 编译期开关的运行期镜像（BuildConfig.h）
#
# BUILD_VISION / BUILD_ENGRAVING 只以 ENABLE_* 宏加在 UI2D / UI3D 目标上，EXE 看不见，
# 于是「这个功能有没有编进来」在运行期无法回答 —— 配置写了、许可放行了、模块没编译，
# 表现就是点了没反应且无日志。启动自检要靠这份镜像做 CMake/JSON/License 三侧交叉核对。
# ---------------------------------------------------------------------------
foreach(_bc_opt
        BUILD_UI3D BUILD_NESTING BUILD_HARDWARE BUILD_VISION BUILD_ENGRAVING
        BUILD_GEOMODELCORE BUILD_RENDERX SANYI_PYTHON_HOST BUILD_PYTHON SANYI_ENABLE_LICENSE)
    if(${_bc_opt})
        set(_bc_${_bc_opt} "true")
    else()
        set(_bc_${_bc_opt} "false")
    endif()
endforeach()
file(READ "${SRC_ROOT}/Common/BuildConfig.h.in" _build_config_content)
string(CONFIGURE "${_build_config_content}" _build_config_configured @ONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/BuildConfig.h" "${_build_config_configured}")

# ---------------------------------------------------------------------------
# qrc 陈旧护栏
#
# configs/ 下新增了客户 JSON 但忘了登记进 configs.qrc 时，构建出的二进制里根本
# 没有这份配置：运行期只会静默回退到 san_yi，既不报错也看不出少了什么。
# 反方向（qrc 里登记了已删除的文件）rcc 自己会报错，不需要额外护栏。
# ---------------------------------------------------------------------------
file(GLOB _client_config_json RELATIVE "${SRC_ROOT}/UI/ClientConfig"
     "${SRC_ROOT}/UI/ClientConfig/configs/*.json")
file(READ "${SRC_ROOT}/UI/ClientConfig/configs.qrc" _configs_qrc_content)
set(_configs_qrc_missing "")
foreach(_cfg ${_client_config_json})
    string(FIND "${_configs_qrc_content}" "${_cfg}" _cfg_pos)
    if(_cfg_pos EQUAL -1)
        list(APPEND _configs_qrc_missing "${_cfg}")
    endif()
endforeach()
if(_configs_qrc_missing)
    message(FATAL_ERROR
        "[Main] configs.qrc 已陈旧：以下客户配置存在于磁盘但未登记进资源，"
        "打包后运行期取不到、会静默回退到默认客户：\n  ${_configs_qrc_missing}\n"
        "请在 ${SRC_ROOT}/UI/ClientConfig/configs.qrc 里补上对应 <file alias=...> 条目。")
endif()


# macOS: 生成 Info.plist。NSHighResolutionCapable 必须为 YES：非 bundle 的裸可执行文件
# 没有 Info.plist，macOS 会以 1x（低分辨率）合成窗口，而 Qt6 按 Retina(dpr=2) 渲染，
# 导致 GL 内容错位缩放并残留在视图角落（重复台面/网格、不均匀网格线）。
if(APPLE)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" _info_plist_content)
    string(CONFIGURE "${_info_plist_content}" _info_plist_configured @ONLY)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" "${_info_plist_configured}")
endif()

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
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${app_name}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.sanyi.${app_name}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
)

target_compile_features(${app_name} PRIVATE cxx_std_17)

# Renderx 运行时资源：着色器与默认字体必须位于应用可执行文件目录
# （applicationDirPath()）。Renderx 自身只复制到其 dylib 输出目录，
# macOS bundle 化后应用二进制位于 Contents/MacOS，需额外复制一份。
if(SANYI_RENDERX_DIR AND EXISTS "${SANYI_RENDERX_DIR}/src/shader")
    file(GLOB _renderx_shader_files
        "${SANYI_RENDERX_DIR}/src/shader/*.vert"
        "${SANYI_RENDERX_DIR}/src/shader/*.frag"
        "${SANYI_RENDERX_DIR}/src/shader/*.comp")
    file(GLOB _renderx_font_files "${SANYI_RENDERX_DIR}/src/res/*.ttf")
    add_custom_command(TARGET ${app_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app_name}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_renderx_shader_files} "$<TARGET_FILE_DIR:${app_name}>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_renderx_font_files} "$<TARGET_FILE_DIR:${app_name}>/"
        COMMENT "Copying Renderx shaders and fonts to app directory"
        VERBATIM
    )
endif()

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
        RenderX
        UICommon
        UI2D
        Log
        FileIO
        ${QT_LIBRARIES}
)

# 客户化 UI 配置
# 注意（P0-1）：SANYI_CLIENT_ID 已不再作为编译期宏注入。
# 客户 ID 现由 UiClientContext 在运行时解析（环境变量 / QSettings / 默认值），
# 同一份二进制可服务多个客户。这里只把默认客户 ID 传给安装包脚本使用。
target_compile_definitions(${app_name} PRIVATE SANYI_DEFAULT_CLIENT_ID="${SANYI_DEFAULT_CLIENT_ID}")

# 可选模块宏传播：保证 EXE 中与各 UI DLL 中 #ifdef 分支一致
if(BUILD_NESTING)
    target_compile_definitions(${app_name} PRIVATE ENABLE_NESTING)
    target_link_libraries(${app_name} PRIVATE Nesting)
endif()

# 硬件抽象层：DeviceHost / MachineProfile 需要直接使用 Hw:: 接口装配设备。
# 与 UI2D 用同一个 ENABLE_HARDWARE 宏名，保证两侧 #ifdef 分支一致。
if(BUILD_HARDWARE)
    target_compile_definitions(${app_name} PRIVATE ENABLE_HARDWARE)
    target_include_directories(${app_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../Hardware/Hardware/Include"
    )
    target_link_libraries(${app_name} PRIVATE Hardware)
endif()


if(BUILD_UI3D)
    target_link_libraries(${app_name} PRIVATE UI3D Engine3D)
    target_compile_definitions(${app_name} PRIVATE BUILD_UI3D=1)
    # 显式传播 ENABLE_GEOMODELCORE：SceneEditService3D / ServicePack3D 等导出类
    # 含 #ifdef ENABLE_GEOMODELCORE 条件数据成员，EXE 必须与 DLL 的 sizeof 一致
    if(BUILD_GEOMODELCORE)
        target_compile_definitions(${app_name} PRIVATE ENABLE_GEOMODELCORE)
        target_link_libraries(${app_name} PRIVATE GeoModelCore)
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
    # dbghelp：崩溃回调中符号化调用栈（CrashHandlerBootstrap.cpp）。
    # 现场机器普遍没有 WinDbg/cdb，只留 .dmp 等于没有线索，
    # 因此崩溃时直接把带函数名与行号的调用栈打到 stderr。
    target_link_libraries(${app_name} PRIVATE dbghelp)
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
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*RenderWidget3DAdapterTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*Scene3DRegressionTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*FrameworkLifecycleTests\\.cpp$")
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*TransformGizmo3DHitTests\\.cpp$")
        # 跨镜像 RTTI 守卫用 Engine3D 的图元做样本，没有 UI3D 时 Engine3D 不参与链接
        list(FILTER MAIN_TEST_SOURCES EXCLUDE REGEX ".*CrossImageRttiTests\\.cpp$")
    endif()

        set(MAIN_TEST_DEP_SOURCES ${MAIN_APP_SOURCES})
        # 测试目标必须使用 gtest_main 作为入口，而不能复用应用本身的 main.cpp
        # （否则 gtest 发现阶段会真正启动 GUI 应用并卡死，导致构建超时失败）。
        list(FILTER MAIN_TEST_DEP_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

    # P2 测试扩展: 添加 protobuf 生成的源文件以支持 SyEntitySerializer 测试
    # 生成目录由 FileIO target 通过 INTERFACE_SANYI_PROTO_GEN_DIR 属性暴露，位于构建树中
    get_target_property(_proto_gen_dir FileIO INTERFACE_SANYI_PROTO_GEN_DIR)
    if(NOT _proto_gen_dir)
        message(FATAL_ERROR "FileIO target does not expose INTERFACE_SANYI_PROTO_GEN_DIR property, cannot locate protobuf generated sources")
    endif()
    set(PROTO_GEN_SRC "${_proto_gen_dir}/SanYiDocument.pb.cc")
    # 标记为生成文件，避免 CMake 配置阶段检查文件是否存在（实际由 FileIO 的 protobuf_generate 在构建期产出）
    set_source_files_properties(${PROTO_GEN_SRC} PROPERTIES GENERATED TRUE)

    # P5 测试扩展: 添加 BaseTool.cpp（非导出类，UI2D 库不导出其符号）
    set(BASE_TOOL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../UI/2D/Src/Ui/DrawTools/BaseTool.cpp")

    find_package(protobuf CONFIG REQUIRED)

    add_executable(MainTests
        ${MAIN_TEST_SOURCES}
        ${MAIN_TEST_DEP_SOURCES}
        ${PROTO_GEN_SRC}
        ${BASE_TOOL_SRC}
        ${SRC_ROOT}/UI/ClientConfig/ExampleCustomPanel.h
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
            # protobuf 生成头文件目录由 FileIO target 的 PUBLIC_INCLUDE_DIRS 自动传播
    )

    target_link_libraries(MainTests
        PRIVATE
            Utility
            CrashHandler
            License
            EngineCommon
            Engine2D
            EnginePersistence
            UICommon
            UI2D
            Log
            RenderX
            FileIO
            protobuf::libprotobuf-lite
            ${QT_LIBRARIES}
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
    )

    if(WIN32)
        # MainTests 编译了全部 MAIN_APP_SOURCES，其中 CrashHandlerBootstrap.cpp 的
        # printSymbolizedBacktrace 依赖 dbghelp。主程序在上面已单独链接，测试目标必须同步，
        # 否则出现 LNK2019 __imp_SymInitialize。
        target_link_libraries(MainTests PRIVATE dbghelp)
    endif()


    if(BUILD_UI3D)
        target_link_libraries(MainTests PRIVATE Engine3D UI3D)
        target_compile_definitions(MainTests PRIVATE BUILD_UI3D=1)
        if(BUILD_GEOMODELCORE)
            target_compile_definitions(MainTests PRIVATE ENABLE_GEOMODELCORE)
            target_link_libraries(MainTests PRIVATE GeoModelCore)
        endif()
    endif()

    if(BUILD_NESTING)
        target_compile_definitions(MainTests PRIVATE ENABLE_NESTING)
        target_link_libraries(MainTests PRIVATE Nesting)
    endif()

    # 硬件抽象层：必须与应用目标用同一套宏，否则 DeviceHost.cpp 在两个目标里
    # 编出来的是不同实现（一个真装配、一个空壳），测试就测不到真实行为。
    if(BUILD_HARDWARE)
        target_compile_definitions(MainTests PRIVATE ENABLE_HARDWARE)
        target_include_directories(MainTests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../Hardware/Hardware/Include"
        )
        target_link_libraries(MainTests PRIVATE Hardware)
    endif()


    # 客户化 UI 配置（测试目标同样需要访问资源）
    # 客户 ID 在运行时解析，测试可通过 UiClientContext::setClientIdOverride() 指定
    target_compile_definitions(MainTests PRIVATE SANYI_DEFAULT_CLIENT_ID="${SANYI_DEFAULT_CLIENT_ID}")

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

    gtest_discover_tests(MainTests DISCOVERY_TIMEOUT 120)

    message(STATUS "[Main Tests]")
    message(STATUS "")
endif()