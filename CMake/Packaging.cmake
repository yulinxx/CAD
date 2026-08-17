# ============================================================================
# SanYi CAD - CPack Packaging Configuration
# ============================================================================
# Usage:
#   cmake --build . --config Release
#   cpack -C Release -G NSIS          (Windows)
#   cpack -C Release -G DragNDrop     (macOS)
#   cpack -C Release -G DEB           (Linux Debian/Ubuntu)
#   cpack -C Release -G RPM           (Linux RHEL/CentOS)
#   cpack -C Release -G AppImage      (Linux universal)
# ============================================================================

include(InstallRequiredSystemLibraries)

# ============================================================================
# Common Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "SanYiCAD")
set(CPACK_PACKAGE_VENDOR "SanYi Technology")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SanYi CAD - Laser Processing Design System")
set(CPACK_PACKAGE_DESCRIPTION "SanYi CAD is an industrial-grade laser processing CAD/CAM system supporting engraving, cutting, and marking workflows.")
set(CPACK_PACKAGE_CONTACT "support@sanyi-cad.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://www.sanyi-cad.com")

set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

set(CPACK_RESOURCE_FILE_LICENSE "${SANYI_ROOT}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${SANYI_ROOT}/README.md")

# ============================================================================
# Install destinations
# ============================================================================
if(WIN32)
    set(CPACK_INSTALL_PREFIX "SanYiCAD")
elseif(APPLE)
    set(CPACK_INSTALL_PREFIX "/Applications/SanYiCAD")
else()
    set(CPACK_INSTALL_PREFIX "/opt/SanYiCAD")
endif()

# ============================================================================
# Windows - NSIS Configuration
# ============================================================================
if(WIN32)
    set(CPACK_GENERATOR "NSIS")

    # NSIS specific settings
    set(CPACK_NSIS_DISPLAY_NAME "SanYi CAD")
    set(CPACK_NSIS_PACKAGE_NAME "SanYiCAD")
    set(CPACK_NSIS_MANUFACTURER "SanYi Technology")
    set(CPACK_NSIS_HELP_LINK "https://www.sanyi-cad.com/support")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://www.sanyi-cad.com")
    set(CPACK_NSIS_CONTACT "support@sanyi-cad.com")

    # Installation directory
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_DISPLAY_SIZE "2000")

    # Start menu and desktop shortcuts
    set(CPACK_NSIS_MENU_LINKS "https://www.sanyi-cad.com" "SanYi CAD Website")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\SanYiCAD.lnk' '$INSTDIR\\\\bin\\\\SanYiCAD.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\SanYiCAD.lnk'"
    )

    # Uninstaller
    set(CPACK_NSIS_UNINSTALL_DISPLAY_NAME "SanYi CAD")
    set(CPACK_NSIS_UNINSTALL_DESCRIPTION "Uninstall SanYi CAD")

    # Extra installation commands
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "WriteRegStr HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD \\\\\"DisplayName\\\" \\\\\"SanYi CAD\\\""
        "WriteRegStr HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD \\\\\"UninstallString\\\" \\\\\"$\\\\\\\"$INSTDIR\\\\uninstall.exe$\\\\\\\"\\\""
    )

    # Extra uninstallation commands
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "DeleteRegKey HKLM\\\\Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\SanYiCAD"
    )

    # Icon for installer
    # set(CPACK_NSIS_INST_ICON "path/to/installer.ico")
    # set(CPACK_NSIS_UNINST_ICON "path/to/uninstaller.ico")

# ============================================================================
# macOS - DragNDrop Configuration
# ============================================================================
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")

    set(CPACK_DMG_VOLUME_NAME "SanYiCAD ${CPACK_PACKAGE_VERSION}")
    set(CPACK_DMG_FORMAT "ULFO")
    set(CPACK_DMG_BACKGROUND_IMAGE "")  # Optional: set path to background image
    set(CPACK_DMG_WINDOW_SIZE "600,400")
    set(CPACK_DMG_VOLUME_BACKGROUND_COLOR "white")

    # Applications symlink
    set(CPACK_DMG_ADD_VOLUME_MOUNT_ICON "")

# ============================================================================
# Linux - DEB Configuration
# ============================================================================
else()
    set(CPACK_GENERATOR "DEB;RPM;AppImage")

    # DEB specific settings
    set(CPACK_DEBIAN_PACKAGE_NAME "sanyicad")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://www.sanyi-cad.com")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 (>= 6.0), libqt6gui6 (>= 6.0), libqt6widgets6 (>= 6.0), "
        "libqt6network6 (>= 6.0), libqt6opengl6 (>= 6.0), libqt6svg6 (>= 6.0), "
        "libgl1-mesa-glx, libssl3"
    )
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "SanYi CAD - Laser Processing Design System\n Industrial-grade laser processing CAD/CAM system supporting\n engraving, cutting, and marking workflows.")

    # RPM specific settings
    set(CPACK_RPM_PACKAGE_NAME "sanyicad")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Graphics")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
    set(CPACK_RPM_PACKAGE_URL "https://www.sanyi-cad.com")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "SanYi CAD - Laser Processing Design System")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "qt6-qtbase >= 6.0, qt6-qtsvg >= 6.0, mesa-libGL >= 1.0, openssl >= 3.0"
    )
    set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")

    # AppImage specific settings
    set(CPACK_APPIMAGE_APP_RUN_PATH "bin/SanYiCAD")
    set(CPACK_APPIMAGE_ICON "resources/icons/sanyicad.png")
endif()

# ============================================================================
# Component-based packaging (optional)
# ============================================================================
set(CPACK_COMPONENTS_ALL Runtime Libraries Translations Resources Python)

set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "SanYi CAD Application")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Main application executable and core DLLs")
set(CPACK_COMPONENT_RUNTIME_GROUP "Runtime")

set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Shared libraries and plugins")
set(CPACK_COMPONENT_LIBRARIES_GROUP "Runtime")

set(CPACK_COMPONENT_TRANSLATIONS_DISPLAY_NAME "Translations")
set(CPACK_COMPONENT_TRANSLATIONS_DESCRIPTION "Language translation files")
set(CPACK_COMPONENT_TRANSLATIONS_GROUP "Resources")

set(CPACK_COMPONENT_RESOURCES_DISPLAY_NAME "Resources")
set(CPACK_COMPONENT_RESOURCES_DESCRIPTION "Application resources and styles")
set(CPACK_COMPONENT_RESOURCES_GROUP "Resources")

set(CPACK_COMPONENT_PYTHON_DISPLAY_NAME "Python Integration")
set(CPACK_COMPONENT_PYTHON_DESCRIPTION "Python scripting and plugin support")
set(CPACK_COMPONENT_PYTHON_GROUP "Runtime")

# ============================================================================
# CPack variables
# ============================================================================
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "sanyicad-${CPACK_PACKAGE_VERSION}-src")

# ============================================================================
# Include CPack (must be last after all CPACK variables are set)
# ============================================================================
# CMake 4.3 configure_file 兼容性问题：CPack 内部调用 configure_file 失败
# include(CPack)
