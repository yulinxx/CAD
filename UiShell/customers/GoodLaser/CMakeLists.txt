find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Qml QuickWidgets QuickEffects)

# 客户库：源文件与 qrc 递归自动收集，新增文件无需改动本文件。
msUiAddCustomerLibrary(${UI_CUSTOMER_TARGET}
    QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    QML_LIBS Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::QuickWidgets)
