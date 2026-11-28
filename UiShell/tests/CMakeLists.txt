find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Test)

# ------------------------------------------------------------
# 目标名：测试目标一律由"前缀 + 短名"生成，改名只动前缀。
# 调用处只写短名（如 Jsonc），完整目标名用 msUiTestTargetName 取。
# ------------------------------------------------------------
set(UI_TEST_TARGET_PREFIX "UiTst")
set(UI_TESTHELPERS_TARGET "UiTestHelpers")

function(msUiTestTargetName out name)
    set(${out} "${UI_TEST_TARGET_PREFIX}${name}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------
# 测试替身：交互与标题栏测试共用，只暴露 public 头。
# ------------------------------------------------------------
# TestHelpers 仅供测试用，硬编码 STATIC，不需要导出符号或 UI_BUILD_SHARED 判断。
add_library(${UI_TESTHELPERS_TARGET} STATIC
    testHelpers/TestHelpers.cpp
)
target_include_directories(${UI_TESTHELPERS_TARGET} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/testHelpers")
target_compile_definitions(${UI_TESTHELPERS_TARGET} PRIVATE
    UI_TEST_TRANSLATIONS_DIR="${UI_QM_DIR}")
target_link_libraries(${UI_TESTHELPERS_TARGET} PUBLIC
    ${UI_SHELL_TARGET}
    Qt${QT_VERSION_MAJOR}::Widgets)
add_dependencies(${UI_TESTHELPERS_TARGET} ${UI_TRANSLATIONS_TARGET})

# ------------------------------------------------------------
# 统一测试目标：一个可执行文件对应一个 ctest 条目。
#   NAME    短名（实际目标名为 <前缀><短名>）
#   SOURCES 源文件
#   LIBS    额外链接库（Qt::Test 由函数自动链接）
#   LABELS  ctest 标签：unit / integration
#   GUI     需要 QApplication：额外链接 Widgets 并强制 offscreen
# ------------------------------------------------------------
function(msUiAddTest)
    cmake_parse_arguments(ARG "GUI" "" "NAME;SOURCES;LIBS;LABELS" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_SOURCES)
        message(FATAL_ERROR "msUiAddTest requires NAME and SOURCES")
    endif()
    if(NOT ARG_LABELS)
        set(ARG_LABELS "unit")
    endif()

    msUiTestTargetName(_tst ${ARG_NAME})

    add_executable(${_tst} ${ARG_SOURCES})
    target_link_libraries(${_tst} PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        ${ARG_LIBS})
    target_include_directories(${_tst} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/testHelpers")

    add_test(NAME ${_tst} COMMAND ${_tst})
    set_tests_properties(${_tst} PROPERTIES LABELS "${ARG_LABELS}")

    if(ARG_GUI)
        target_link_libraries(${_tst} PRIVATE
            Qt${QT_VERSION_MAJOR}::Widgets)
        # QT_PLUGIN_PATH 指向 <Qt>/plugins（UiProjectDefaults.cmake 从 qmake 推导）。
        # Qt6::Gui 没有 PLUGIN_DIR 目标属性，原生成表达式求值为空，会导致 offscreen 等插件找不到。
        set_tests_properties(${_tst} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${UI_QT_PLUGIN_DIR}")
    endif()

    # Qt DLL 位于 <Qt>/bin，加入测试进程 PATH 后可一条命令跑完 ctest。
    if(UI_QT_BIN_DIR)
        set_tests_properties(${_tst} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${UI_QT_BIN_DIR}")
    endif()
endfunction()

# ------------------------------------------------------------
# 模型层测试：纯 QtCore，不需要 QApplication / QtWidgets。
# Model 不导出，测试直接链接内部静态库 UiShellModel。
# ------------------------------------------------------------
msUiAddTest(NAME Jsonc
    SOURCES unit/Model/tstJsonc.cpp
    LIBS ${UI_SHELL_TARGET} ${UI_MODEL_TARGET}
    LABELS unit)

msUiAddTest(NAME ProfileLoader
    SOURCES unit/Model/tstProfileLoader.cpp
    LIBS ${UI_SHELL_TARGET} ${UI_MODEL_TARGET}
    LABELS unit)
msUiTestTargetName(_tstProfileLoader ProfileLoader)
# ProfileSchema.h 是 src/Model/ 下的内部头：测试显式加 src 包含路径。
target_include_directories(${_tstProfileLoader} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")

# profile 加载期的命令登记钩子：外置 ui.json 的菜单命令也能进入客户目录。
msUiAddTest(NAME ProfileCommands
    SOURCES unit/Model/tstProfileCommands.cpp
    LIBS ${UI_SHELL_TARGET} ${UI_MODEL_TARGET}
    LABELS unit)
msUiTestTargetName(_tstProfileCommands ProfileCommands)
target_include_directories(${_tstProfileCommands} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")

msUiAddTest(NAME AssemblyPlanner
    SOURCES unit/Model/tstAssemblyPlanner.cpp
    LIBS ${UI_SHELL_TARGET} ${UI_MODEL_TARGET}
    LABELS unit)

msUiAddTest(NAME CustomerProfile
    SOURCES unit/Model/tstCustomerProfile.cpp
    LIBS ${UI_SHELL_TARGET} ${UI_MODEL_TARGET}
    LABELS unit)
msUiTestTargetName(_tstCustomerProfile CustomerProfile)
target_compile_definitions(${_tstCustomerProfile} PRIVATE
    UI_CUSTOMERS_DIR="${UI_SHELL_SOURCE_DIR}/customers")

# ------------------------------------------------------------
# profile 定位：--profile → <exe>/profile/ui.json → 内嵌资源的优先级与报错。
# ProfileLocator 属于入口（main/），仅依赖 QtCore，直接编进测试目标。
# ------------------------------------------------------------
msUiAddTest(NAME ProfileLocator
    SOURCES unit/Launcher/tstProfileLocator.cpp
        "${UI_SHELL_SOURCE_DIR}/main/Launcher/ProfileLocator.cpp"
    LIBS Qt${QT_VERSION_MAJOR}::Core
    LABELS unit)
msUiTestTargetName(_tstProfileLocator ProfileLocator)
target_include_directories(${_tstProfileLocator} PRIVATE "${UI_SHELL_SOURCE_DIR}/main")

# 主窗口：窗口状态保存与恢复（临时 ini，不碰注册表）。MainWindow 属于入口（main/），直接编进测试目标；
# 头文件列进 SOURCES，保证 AUTOMOC 处理其中的 Q_OBJECT。
msUiAddTest(NAME MainWindow
    SOURCES unit/Window/tstMainWindow.cpp
        "${UI_SHELL_SOURCE_DIR}/main/Window/MainWindow.cpp"
        "${UI_SHELL_SOURCE_DIR}/main/Window/MainWindow.h"
    LABELS unit
    GUI)
msUiTestTargetName(_tstMainWindow MainWindow)
target_include_directories(${_tstMainWindow} PRIVATE "${UI_SHELL_SOURCE_DIR}/main")

# 共享对话框库：示例算法（纯函数）、参数 JSON 往返、默认参数对话框。
msUiAddTest(NAME SampleDialogs
    SOURCES unit/Dialogs/tstSampleDialogs.cpp
    LIBS ${UI_DIALOGS_TARGET}
    LABELS unit
    GUI)

# ------------------------------------------------------------
# 翻译测试：纯 QtCore；自定义 main 先建 QCoreApplication 再跑 Qt Test。
# ------------------------------------------------------------
# textContextForKey 是 shell 内部函数（不导出）：把它的源文件直接编进测试。
msUiAddTest(NAME Translation
    SOURCES unit/i18n/tstTranslation.cpp
        "${UI_SHELL_SOURCE_DIR}/src/i18n/TextContext.cpp"
    LIBS ${UI_SHELL_TARGET}
    LABELS unit)
msUiTestTargetName(_tstTranslation Translation)
target_include_directories(${_tstTranslation} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")
target_compile_definitions(${_tstTranslation} PRIVATE
    UI_TEST_TRANSLATIONS_DIR="${UI_QM_DIR}")
add_dependencies(${_tstTranslation} ${UI_TRANSLATIONS_TARGET})

# ------------------------------------------------------------
# 关于对话框测试：offscreen 下断言控件文本，不需要真实窗口。
# ------------------------------------------------------------
# 默认关于框在共享对话框库 UiDialogs 里（shell 不认识它），视图类是库内部头。
msUiAddTest(NAME AboutDialog
    SOURCES unit/Dialogs/tstAboutDialog.cpp
    LIBS ${UI_DIALOGS_TARGET}
    LABELS unit
    GUI)
msUiTestTargetName(_tstAboutDialog AboutDialog)
target_include_directories(${_tstAboutDialog} PRIVATE "${UI_SHELL_SOURCE_DIR}/dialogs/src")

# 对话框打开方式（契约层 runModalDialog）：所有对话框工厂共用，验证确定、取消、父窗口被销毁场景。
msUiAddTest(NAME ModalDialog
    SOURCES unit/Dialogs/tstModalDialog.cpp
    LIBS ${UI_CONTRACT_TARGET}
    LABELS unit
    GUI)

# ------------------------------------------------------------
# 客户自定义关于框：源文件直接编进测试，三套构建都会跑。
# 仅依赖契约、UiDialogs 与 Qt（GoodLaser 另需 Qt Quick），不链接 shell 与客户库。
# ------------------------------------------------------------
# April 菜单目录：直接编译客户的 Commands.cpp，验证 ui.json 菜单节点都能被登记。
set(_UI_April_dir "${UI_SHELL_SOURCE_DIR}/customers/April")
msUiAddTest(NAME AprilMenuCatalog
    SOURCES unit/Customers/tstAprilMenuCatalog.cpp
            "${_UI_April_dir}/commands/Commands.cpp"
    LIBS ${UI_CONTRACT_TARGET}
    LABELS unit)
msUiTestTargetName(_tstAprilMenuCatalog AprilMenuCatalog)
target_include_directories(${_tstAprilMenuCatalog} PRIVATE
    "${_UI_April_dir}"
    "${CMAKE_SOURCE_DIR}/Main/Src")
target_compile_definitions(${_tstAprilMenuCatalog} PRIVATE
    APRIL_PROFILE_PATH="${_UI_April_dir}/ui.json")

set(_UI_TyvokStudio_dir "${UI_SHELL_SOURCE_DIR}/customers/TyvokStudio")
msUiAddTest(NAME TyvokStudioAboutDialog
    SOURCES unit/Customers/tstTyvokStudioAboutDialog.cpp
        "${_UI_TyvokStudio_dir}/dialogs/AboutDialog.cpp"
        "${_UI_TyvokStudio_dir}/dialogs/AboutDialogFactory.cpp"
    LIBS ${UI_DIALOGS_TARGET}
    LABELS unit
    GUI)
msUiTestTargetName(_tstTyvokStudioAbout TyvokStudioAboutDialog)
target_include_directories(${_tstTyvokStudioAbout} PRIVATE "${_UI_TyvokStudio_dir}")

# GoodLaser 的 QML 关于框：没有 Qt Quick 组件时跳过。
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Qml Quick QuickWidgets)
if(TARGET Qt${QT_VERSION_MAJOR}::QuickWidgets)
    set(_UI_GoodLaser_dir "${UI_SHELL_SOURCE_DIR}/customers/GoodLaser")
    # AboutDialog.h 带 Q_OBJECT，列进 SOURCES 让 AUTOMOC 处理；qml.qrc 经 AUTORCC 编进测试。
    msUiAddTest(NAME GoodLaserAboutDialog
        SOURCES unit/Customers/tstGoodLaserAboutDialog.cpp
            "${_UI_GoodLaser_dir}/dialogs/AboutDialog.cpp"
            "${_UI_GoodLaser_dir}/dialogs/AboutDialog.h"
            "${_UI_GoodLaser_dir}/dialogs/AboutDialogFactory.cpp"
            "${_UI_GoodLaser_dir}/qml/qml.qrc"
        LIBS ${UI_DIALOGS_TARGET}
            Qt${QT_VERSION_MAJOR}::Qml
            Qt${QT_VERSION_MAJOR}::Quick
            Qt${QT_VERSION_MAJOR}::QuickWidgets
        LABELS unit
        GUI)
    msUiTestTargetName(_tstGoodLaserAbout GoodLaserAboutDialog)
    target_include_directories(${_tstGoodLaserAbout} PRIVATE "${_UI_GoodLaser_dir}")
    # offscreen 下没有 GPU：Qt Quick 用软件渲染；同时给出 QML 导入路径，
    # 否则 qrc 里的 QML 找不到 QtQuick.Layouts / QtQuick.Effects 等模块。
    set_property(TEST ${_tstGoodLaserAbout} APPEND PROPERTY ENVIRONMENT
        "QT_QUICK_BACKEND=software"
        "QML_IMPORT_PATH=${UI_QT_QML_DIR}")

    # GoodLaser 的 QML 面板：验证 qrc 资源路径与皮肤跟随。QmlPanelWidget.h 带 Q_OBJECT，列进 SOURCES 让 AUTOMOC 处理。
    msUiAddTest(NAME GoodLaserQmlPanel
        SOURCES unit/Customers/tstGoodLaserQmlPanel.cpp
            "${_UI_GoodLaser_dir}/qml/QmlPanelWidget.cpp"
            "${_UI_GoodLaser_dir}/qml/QmlPanelWidget.h"
            "${_UI_GoodLaser_dir}/qml/qml.qrc"
        LIBS ${UI_CONTRACT_TARGET}
            Qt${QT_VERSION_MAJOR}::Qml
            Qt${QT_VERSION_MAJOR}::Quick
            Qt${QT_VERSION_MAJOR}::QuickWidgets
        LABELS unit
        GUI)
    msUiTestTargetName(_tstGoodLaserQmlPanel GoodLaserQmlPanel)
    target_include_directories(${_tstGoodLaserQmlPanel} PRIVATE "${_UI_GoodLaser_dir}")
    set_property(TEST ${_tstGoodLaserQmlPanel} APPEND PROPERTY ENVIRONMENT
        "QT_QUICK_BACKEND=software"
        "QML_IMPORT_PATH=${UI_QT_QML_DIR}")
else()
    message(STATUS "UI tests    : Qt Quick not found, skip GoodLaserAboutDialog / GoodLaserQmlPanel")
endif()

# ------------------------------------------------------------
# 平台层可构造性测试：QTEST_MAIN + offscreen，不拉起真实窗口。
# ------------------------------------------------------------
# 平台层不导出符号：把 UI_PLATFORM_SOURCES（见 src/CMakeLists.txt）直接编进测试，不链接 UiShell。
msUiAddTest(NAME WindowFrame
    SOURCES unit/Platform/tstWindowFrame.cpp ${UI_PLATFORM_SOURCES}
    LIBS ${UI_CONTRACT_TARGET}
    LABELS unit
    GUI)
msUiTestTargetName(_tstWindowFrame WindowFrame)
# WindowFrame.h / HitTest.h 是 src/Platform/ 下的内部头：测试显式加 src 包含路径。
target_include_directories(${_tstWindowFrame} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")

# 边缘缩放过滤器：Linux/macOS 的拖拽缩放全靠 onStartResize 回调，
# 单元测试验证“边缘按下 → 调用回调”的行为。
msUiAddTest(NAME ResizeBorderFilter
    SOURCES unit/Platform/tstResizeBorderFilter.cpp
    LIBS ${UI_SHELL_TARGET}
    LABELS unit
    GUI)
msUiTestTargetName(_tstResizeBorderFilter ResizeBorderFilter)
target_include_directories(${_tstResizeBorderFilter} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")

# 文案刷新模板与面板语义标记：三个客户共用同一份公共实现。
msUiAddTest(NAME Retranslatable
    SOURCES unit/Assembly/tstRetranslatable.cpp
    LIBS ${UI_SHELL_TARGET}
    LABELS unit
    GUI)

# ------------------------------------------------------------
# 标题栏与装配运行期：真实鼠标事件与运行期装配结果。
# ------------------------------------------------------------
msUiAddTest(NAME TitleBar
    SOURCES unit/Titlebar/tstTitleBar.cpp
    LIBS ${UI_TESTHELPERS_TARGET}
    LABELS unit
    GUI)

# tstRuntime.cpp needs to be restored — PowerShell Set-Content corrupted it.
# msUiAddTest(NAME Runtime
#     SOURCES unit/Assembly/tstRuntime.cpp
#     LIBS ${UI_TESTHELPERS_TARGET}
#     LABELS unit
#     GUI)
# msUiTestTargetName(_tstRuntime Runtime)
# target_compile_definitions(${_tstRuntime} PRIVATE
#     UI_TEST_TRANSLATIONS_DIR="${UI_QM_DIR}")
# add_dependencies(${_tstRuntime} ${UI_TRANSLATIONS_TARGET})

# 命令表（契约层辅助类 CommandTable）：目录、执行、状态，纯 QtCore。
msUiAddTest(NAME CommandTable
    SOURCES unit/Assembly/tstCommandTable.cpp
    LIBS ${UI_CONTRACT_TARGET}
    LABELS unit)

# 对话框注册表：纯 QtCore 接口，验证稳定类型键与按接口注册的语义。
msUiAddTest(NAME DialogRegistry
    SOURCES unit/Assembly/tstDialogRegistry.cpp
    LIBS ${UI_SHELL_TARGET}
    LABELS unit)

# 功能开关 store：验证订阅回调在所属线程投递、重复启用/禁用与退订语义。
msUiAddTest(NAME FeatureFlagStore
    SOURCES unit/Assembly/tstFeatureFlagStore.cpp
    LIBS ${UI_SHELL_TARGET}
    LABELS unit)
msUiTestTargetName(_tstFeatureFlagStore FeatureFlagStore)
# FeatureFlagStore.h 是 src/Assembly/Host/ 下的内部头：测试显式加 src 包含路径。
target_include_directories(${_tstFeatureFlagStore} PRIVATE "${UI_SHELL_SOURCE_DIR}/src")

# ApiPtr：验证自重置、替换、析构与移动语义，防止跨 DLL 释放出现双重销毁。
msUiAddTest(NAME ApiPtr
    SOURCES unit/Assembly/tstApiPtr.cpp
    LIBS ${UI_SHELL_TARGET}
    LABELS unit)

# ------------------------------------------------------------
# 集成交互：菜单点击、模态关于对话框、面板激活、语言切换。
# ------------------------------------------------------------
msUiAddTest(NAME Interaction
    SOURCES Integration/tstInteraction.cpp
        resources/tstCustomerStyle.qrc
    LIBS ${UI_TESTHELPERS_TARGET} ${UI_CUSTOMER_TARGET} ${UI_DIALOGS_TARGET}
    LABELS integration
    GUI)
msUiTestTargetName(_tstInteraction Interaction)
target_compile_definitions(${_tstInteraction} PRIVATE
    UI_TEST_TRANSLATIONS_DIR="${UI_QM_DIR}")
add_dependencies(${_tstInteraction} ${UI_TRANSLATIONS_TARGET})

# ------------------------------------------------------------
# 源码级检查（分层依赖、导出面）：实现集中在 cmake/UiChecks.cmake。
# ------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/UiChecks.cmake")
UiAddCheckTests()

# ------------------------------------------------------------
# 测试汇总报告：自定义 target，运行所有测试并输出醒目汇总。
# 运行方式：cmake --build build --target ui-shell-test-summary
# ------------------------------------------------------------
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ui_shell_ctest_summary.sh" "
#!/bin/bash
echo ''
echo '================================================'
echo '               TEST SUMMARY REPORT'
echo '================================================'
${CMAKE_CTEST_COMMAND} --output-on-failure
_EXITCODE=\\$?
echo ''
echo '================================================'
echo 'Build: ${CTEST_PROJECT_NAME} | Time: \\$(date \"+%Y-%m-%d %H:%M:%S\")'
echo '================================================'
exit \\$_EXITCODE
")

add_custom_target(ui-shell-test-summary
    COMMAND chmod +x "${CMAKE_CURRENT_BINARY_DIR}/ui_shell_ctest_summary.sh"
            "${CMAKE_CURRENT_BINARY_DIR}/ui_shell_ctest_summary.sh"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests with summary report"
)
