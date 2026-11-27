# 客户库：源文件与 qrc 递归自动收集，新增文件无需改动本文件。
msUiAddCustomerLibrary(${UI_CUSTOMER_TARGET})

# ---------------------------------------------------------------------------
# M1/M2：复用现有 CAD UI 静态库 AprilUi（Main/Src/UI/**）。
#
# M2 阶段只验证可链接；CAD 视口/命令/资源在后续里程碑接入：
#   M3 ICenterViewProvider::createCenterView() → 现有 Workbench
#   M4 ICommandCatalog / ICommandInvoker → 现有命令表
#   M5 Q_INIT_RESOURCE + profile 外置
# 资源注册由宿主（April 或 AprilShell）负责，本库不在 M2 引入 qrc。
# ---------------------------------------------------------------------------
if(TARGET AprilUi)
    target_link_libraries(${UI_CUSTOMER_TARGET} PRIVATE AprilUi)
endif()
