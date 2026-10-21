# ============================================================================
# SanYi CAD - CPack 打包配置
# ============================================================================
# 使用方法：
#   cmake --build . --config Release
#   cpack -C Release -G NSIS          (Windows)
#   cpack -C Release -G DragNDrop     (macOS)
#   cpack -C Release -G DEB           (Linux Debian/Ubuntu)
#   cpack -C Release -G RPM           (Linux RHEL/CentOS)
#   cpack -C Release -G AppImage      (Linux 通用)
# ============================================================================

include(InstallRequiredSystemLibraries)

# ============================================================================
# 通用配置
# ============================================================================
set(CPACK_PACKAGE_NAME "SanYiCAD")
set(CPACK_PACKAGE_VENDOR "SanYi Technology")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SanYi CAD - 激光加工设计系统")
set(CPACK_PACKAGE_DESCRIPTION "SanYi CAD 是一款工业级激光加工 CAD/CAM 系统，支持雕刻、切割和打标工作流。")
set(CPACK_PACKAGE_CONTACT "support@sanyi-cad.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://www.sanyi-cad.com")

set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

set(CPACK_RESOURCE_FILE_LICENSE "${SANYI_ROOT}/LICENSE.md")
set(CPACK_RESOURCE_FILE_README "${SANYI_ROOT}/README.md")

# ============================================================================
# 安装目标路径
# ============================================================================
if(WIN32)
    set(CPACK_INSTALL_PREFIX "SanYiCAD")
elseif(APPLE)
    set(CPACK_INSTALL_PREFIX "/Applications/SanYiCAD")
else()
    set(CPACK_INSTALL_PREFIX "/opt/SanYiCAD")
endif()

# ============================================================================
# Windows - NSIS 配置
# ============================================================================
if(WIN32)
    set(CPACK_GENERATOR "NSIS")

    # NSIS 专用设置
    set(CPACK_NSIS_DISPLAY_NAME "SanYi CAD")

    # 升级时先调用旧版本的 uninstall.exe，再安装新版本，避免残留旧 DLL。
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_PACKAGE_NAME "SanYiCAD")
    set(CPACK_NSIS_MANUFACTURER "SanYi Technology")
    set(CPACK_NSIS_HELP_LINK "https://www.sanyi-cad.com/support")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://www.sanyi-cad.com")
    set(CPACK_NSIS_CONTACT "support@sanyi-cad.com")

    # 安装目录
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_DISPLAY_SIZE "2000")

    # 开始菜单和桌面快捷方式
    set(CPACK_NSIS_MENU_LINKS "https://www.sanyi-cad.com" "SanYi CAD 官网")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\SanYiCAD.lnk' '$INSTDIR\\\\bin\\\\SanYiCAD.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\SanYiCAD.lnk'"
    )

    # 卸载程序
    set(CPACK_NSIS_UNINSTALL_DISPLAY_NAME "SanYi CAD")
    set(CPACK_NSIS_UNINSTALL_DESCRIPTION "卸载 SanYi CAD")

    # 额外安装命令
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "WriteRegStr HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD \\\\\"DisplayName\\\" \\\\\"SanYi CAD\\\""
        "WriteRegStr HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD \\\\\"UninstallString\\\" \\\\\"$\\\\\\\"$INSTDIR\\\\uninstall.exe$\\\\\\\"\\\""
    )

    # 额外卸载命令
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD"
    )

    # 安装程序图标
    # set(CPACK_NSIS_INST_ICON "path/to/installer.ico")
    # set(CPACK_NSIS_UNINST_ICON "path/to/uninstaller.ico")

# ============================================================================
# macOS - DragNDrop 配置
# ============================================================================
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")

    set(CPACK_DMG_VOLUME_NAME "SanYiCAD ${CPACK_PACKAGE_VERSION}")
    set(CPACK_DMG_FORMAT "ULFO")
    set(CPACK_DMG_BACKGROUND_IMAGE "")  # 可选：设置背景图片路径
    set(CPACK_DMG_WINDOW_SIZE "600,400")
    set(CPACK_DMG_VOLUME_BACKGROUND_COLOR "white")

    # 应用程序符号链接
    set(CPACK_DMG_ADD_VOLUME_MOUNT_ICON "")

# ============================================================================
# Linux - DEB 配置
# ============================================================================
else()
    set(CPACK_GENERATOR "DEB;RPM;AppImage")

    # DEB 专用设置
    set(CPACK_DEBIAN_PACKAGE_NAME "sanyicad")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://www.sanyi-cad.com")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 (>= 6.0), libqt6gui6 (>= 6.0), libqt6widgets6 (>= 6.0), "
        "libqt6network6 (>= 6.0), libqt6opengl6 (>= 6.0), libqt6svg6 (>= 6.0), "
        "libgl1-mesa-glx, libssl3"
    )
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "SanYi CAD - 激光加工设计系统\n 工业级激光加工 CAD/CAM 系统，支持\n 雕刻、切割和打标工作流。")

    # RPM 专用设置
    set(CPACK_RPM_PACKAGE_NAME "sanyicad")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Graphics")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
    set(CPACK_RPM_PACKAGE_URL "https://www.sanyi-cad.com")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "SanYi CAD - 激光加工设计系统")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "qt6-qtbase >= 6.0, qt6-qtsvg >= 6.0, mesa-libGL >= 1.0, openssl >= 3.0"
    )
    set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")

    # AppImage 专用设置
    set(CPACK_APPIMAGE_APP_RUN_PATH "bin/SanYiCAD")
    set(CPACK_APPIMAGE_ICON "resources/icons/sanyicad.png")
endif()

# ============================================================================
# 组件策略
# ============================================================================
# 这里刻意不设置 CPACK_COMPONENTS_ALL：
#   install(TARGETS SanYiCAD ...) 没有写 COMPONENT，会被归入 "Unspecified"。
#   一旦设置 CPACK_COMPONENTS_ALL 却漏掉 Unspecified，CPack 会“成功”打出一个
#   不含主程序/核心 DLL 的安装包。当前安装包按单组件整体交付，先保持全量打包，
#   等真正需要可选组件时再逐项补齐组件归属。
#
# 安装目录必须稳定（不能随版本号变化），否则 NSIS 升级会装成并存的两个目录，
# 而不是覆盖升级。
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SanYiCAD")

# ============================================================================
# CPack 变量
# ============================================================================
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "sanyicad-${CPACK_PACKAGE_VERSION}-src")

# ============================================================================
# 包含 CPack（必须在所有 CPACK 变量设置之后）
# ============================================================================
include(CPack)
