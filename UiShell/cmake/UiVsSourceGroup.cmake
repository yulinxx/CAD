# VS 解决方案资源管理器虚拟文件夹：source_group(TREE <UI_SHELL_SOURCE_DIR>) 自动按物理路径建嵌套组。
#
# 注意：生成文件（如 UiCustomerProfile 在 build/ 下生成的 .qrc）不在源码树内，
# source_group(TREE ...) 遇到树外文件会直接报错，因此这里只把源码树内的文件
# 交给 source_group，其余文件保持普通列表显示。
function(UiVsSourceGroup TARGET)
    get_target_property(_srcs ${TARGET} SOURCES)
    if(NOT _srcs)
        return()
    endif()

    set(_in_tree "")
    foreach(_f IN LISTS _srcs)
        # 生成表达式（$<TARGET_OBJECTS:...> 等）没有真实路径，跳过。
        if(_f MATCHES "^\\$<")
            continue()
        endif()
        get_filename_component(_abs "${_f}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        string(FIND "${_abs}" "${UI_SHELL_SOURCE_DIR}/" _pos)
        if(_pos EQUAL 0)
            list(APPEND _in_tree "${_abs}")
        endif()
    endforeach()

    if(_in_tree)
        source_group(TREE "${UI_SHELL_SOURCE_DIR}" FILES ${_in_tree})
    endif()
endfunction()
