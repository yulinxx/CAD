# 导出面回归检查
# 必需的工厂函数必须在导出表中
# 具体实现类符号不得出现在导出表中（误加导出宏即失败）。
# 仅做名字级检查，用 "@ClassName@" 作为特征串。
if(NOT UI_DLL)
    message(FATAL_ERROR "UI_DLL is not set")
endif()
if(NOT UI_DUMPBIN)
    message(FATAL_ERROR "UI_DUMPBIN is not set")
endif()
if(NOT EXISTS "${UI_DLL}")
    message(FATAL_ERROR "UiShell.dll not found: ${UI_DLL}")
endif()

execute_process(
    COMMAND "${UI_DUMPBIN}" /nologo /dump /exports "${UI_DLL}"
    OUTPUT_VARIABLE _dump
    ERROR_VARIABLE _dump_error
    RESULT_VARIABLE _dump_result)
if(NOT _dump_result EQUAL 0)
    message(FATAL_ERROR "dumpbin failed (${_dump_result}): ${_dump_error}")
endif()

# 客户 DLL 独立检查：必需入口、指纹 + 禁止导出具体类。
if(UI_EXPECT_CUSTOMER)
    set(_required
        "createCustomerModule@"
        "customerBuildFingerprint@")
    # 客户实现都在各自命名空间里：按命名空间禁止，新增类自动覆盖。
    # 命名空间：MantiSoft → MS，GoodLaser → GL，TyvokStudio → TS。
    # 两字母命名空间易误匹配，按 MSVC 修饰名的 "@NS@@" 形式精确匹配。
    set(_forbidden
        "@MS@@"
        "@TS@@"
        "@GL@@"
        "@CustomerDialogFactory@")
else()
    # shell DLL：必需工厂函数、指纹；禁止导出具体实现类。
    set(_required
        "createTranslationService@"
        "createQtTextProvider@"
        "createTitleBar@"
        "createShellRuntime@"
        "createWidgetFactoryRegistry@"
        "createDialogRegistry@"
        "createFeatureFlagStore@"
        "shellBuildFingerprint@")

    set(_forbidden
        "@ShellRuntime@"
        "@ShellView@"
        "@ProfilePipeline@"
        "@RegionContainer@"
        "@ProfileParser@"
        "@ProfileValidator@"
        "@AssemblyPlanner@"
        "@TranslationService@"
        "@QtTextProvider@"
        "@TitleBar@"
        "@WidgetFactoryRegistry@"
        "@DialogRegistry@"
        "@FeatureFlagStore@"
        "@TitleBarHostAdapter@"
        "@Jsonc@"
        # 平台层与 i18n 内部函数只在 shell 内使用，不得进入导出表。
        "createWindowFrame@"
        "createWindowCapabilities@"
        "textContextForKey@")
endif()

set(_violations "")

foreach(_symbol IN LISTS _required)
    string(FIND "${_dump}" "${_symbol}" _found)
    if(_found EQUAL -1)
        list(APPEND _violations "missing export: ${_symbol}*")
    endif()
endforeach()

foreach(_symbol IN LISTS _forbidden)
    string(FIND "${_dump}" "${_symbol}" _found)
    if(NOT _found EQUAL -1)
        list(APPEND _violations "concrete class exported: ${_symbol}")
    endif()
endforeach()

list(LENGTH _violations _count)
if(_count GREATER 0)
    message(STATUS "Export surface check failed with ${_count} violation(s):")
    foreach(_v IN LISTS _violations)
        message(STATUS "  ${_v}")
    endforeach()
    message(FATAL_ERROR "Export surface regressed")
endif()

message(STATUS "Export surface check passed (${UI_DLL})")
