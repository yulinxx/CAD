# 源码级检查：分层依赖与导出面。由 tests/CMakeLists.txt 调用注册为 ctest。
# 脚本本体在 cmake/CheckLayers.cmake 与 cmake/CheckExports.cmake。

set(UI_LAYER_CHECK_TARGET  "UiLayerCheck")
set(UI_EXPORT_CHECK_TARGET "UiExportCheck")
set(_UI_check_script_dir "${CMAKE_CURRENT_LIST_DIR}")

function(UiAddCheckTests)
    add_test(NAME ${UI_LAYER_CHECK_TARGET}
        COMMAND ${CMAKE_COMMAND}
            -DUI_SOURCE_DIR=${UI_SHELL_SOURCE_DIR}
            -P "${_UI_check_script_dir}/CheckLayers.cmake")

    if(WIN32 AND UI_BUILD_SHARED)
        # dumpbin 与 link.exe 同目录；优先用 dumpbin，缺失时退回 link /dump。
        get_filename_component(_linker_dir "${CMAKE_LINKER}" DIRECTORY)
        if(EXISTS "${_linker_dir}/dumpbin.exe")
            set(_dumpbin "${_linker_dir}/dumpbin.exe")
        else()
            set(_dumpbin "${CMAKE_LINKER}")
        endif()

        add_test(NAME ${UI_EXPORT_CHECK_TARGET}
            COMMAND ${CMAKE_COMMAND}
                -DUI_DLL=$<TARGET_FILE:${UI_SHELL_TARGET}>
                -DUI_DUMPBIN=${_dumpbin}
                -P "${_UI_check_script_dir}/CheckExports.cmake")
        set_tests_properties(${UI_EXPORT_CHECK_TARGET} PROPERTIES LABELS "unit")

        # 客户 DLL：必须导出 ABI 校验、模块入口和指纹；禁止导出具体工厂类。
        add_test(NAME UiCustomerExportCheck
            COMMAND ${CMAKE_COMMAND}
                -DUI_DLL=$<TARGET_FILE:${UI_CUSTOMER_TARGET}>
                -DUI_DUMPBIN=${_dumpbin}
                -DUI_EXPECT_CUSTOMER=ON
                -P "${_UI_check_script_dir}/CheckExports.cmake")
        set_tests_properties(UiCustomerExportCheck PROPERTIES LABELS "unit")
    endif()
endfunction()
