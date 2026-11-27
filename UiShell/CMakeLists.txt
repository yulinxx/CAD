# ============================================================
# UiShell：以 SampleFrame 为蓝本的配置驱动 UI 外壳。
#
# 本目录是 April 根工程的一个子目录（不调用 project()），
# 复用根工程已解析的 Qt6 / vcpkg / 编译器设置，输出到独立目录：
#   <build>/bin_Qt6/shell/    EXE 与动态库
#   <build>/lib_Qt6/shell/    静态库
#
# 迁移策略：新外壳先与现有 Main 并行存在，Main 保持可运行；
# 随后按里程碑把 Main 的工作台/视口/面板逐步搬到 customers/April。
# ============================================================

set(UI_SHELL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(UI_SHELL_BUILD_DIR  "${CMAKE_CURRENT_BINARY_DIR}")
set(UI_SHELL_VERSION    "${PROJECT_VERSION}")
if(NOT UI_SHELL_VERSION)
    set(UI_SHELL_VERSION "1.0.0")
endif()

# 根工程开启了 CMAKE_UNITY_BUILD；外壳代码按“一文件一符号”组织，
# 合批会制造静态符号冲突，这里对本子目录关闭。
set(CMAKE_UNITY_BUILD OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiProjectDefaults.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiBuildInfo.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiTranslations.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiDeploy.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiCustomerProfile.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiVsSourceGroup.cmake")

add_subdirectory(src)
# 共享对话框库（静态）：算法与业务对话框的默认实现，链进客户库。
add_subdirectory(dialogs)
add_subdirectory("customers/${_UI_customer_dir}")
add_subdirectory(main)

# 测试：根工程用 BUILD_TESTS；独立构建时也接受 SampleFrame 的 Enable_TEST。
if(BUILD_TESTS OR Enable_TEST)
    enable_testing()
    add_subdirectory(tests)
endif()
