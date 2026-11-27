# 客户库：源文件与 qrc 递归自动收集，新增文件无需改动本文件。
msUiAddCustomerLibrary(${UI_CUSTOMER_TARGET})

# ---------------------------------------------------------------------------
# M1/M2：复用现有 CAD UI 静态库 AprilUi（Main/Src/UI/**）。
# M3：AprilWorkbench 把现有 RenderViewport2D / Viewport3D 框架移植到
#     ICenterViewProvider::createCenterView；视口服务由本客户持有并注入。
#
# 资源注册由宿主（April 或 AprilShell）负责；本库不在此引入 qrc。
# 本轮“框架移植”不保证编译；Engine 子模块处理后再一并验证。
# ---------------------------------------------------------------------------
if(TARGET AprilUi)
    target_link_libraries(${UI_CUSTOMER_TARGET} PRIVATE AprilUi)
endif()
