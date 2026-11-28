# 翻译产物：<模块>/<库名>_<语言>.ts → <build>/UiShell/translations/<库名>_<语言>.qm，
# 宿主 TranslationService 按 <库名>_<语言>.qm 查找。
# 源文件分三组：框架 translations/UiShell/*.ts、共享对话框 dialogs/translations/*.ts、
# 客户 customers/*/translations/*.ts。

file(GLOB _UI_shell_ts CONFIGURE_DEPENDS
    "${UI_SHELL_SOURCE_DIR}/translations/UiShell/*.ts")
file(GLOB _UI_customer_ts CONFIGURE_DEPENDS
    "${UI_SHELL_SOURCE_DIR}/customers/*/translations/*.ts")
file(GLOB _UI_dialogs_ts CONFIGURE_DEPENDS
    "${UI_SHELL_SOURCE_DIR}/dialogs/translations/*.ts")
set(UI_TS_FILES ${_UI_shell_ts} ${_UI_dialogs_ts} ${_UI_customer_ts})

set(UI_QM_DIR "${UI_SHELL_BUILD_DIR}/translations")
set_source_files_properties(${UI_TS_FILES} PROPERTIES OUTPUT_LOCATION "${UI_QM_DIR}")

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_translation(UI_QM_FILES ${UI_TS_FILES})
else()
    qt5_add_translation(UI_QM_FILES ${UI_TS_FILES})
endif()
add_custom_target(${UI_TRANSLATIONS_TARGET} ALL DEPENDS ${UI_QM_FILES})
