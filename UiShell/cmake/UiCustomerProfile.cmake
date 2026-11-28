# 客户 profile（ui.json + assets/）的两条交付路径，由各客户 CMakeLists.txt 在 add_library 之后调用：
#   UiCustomerProfile(<customer-target>)
# 1. 内嵌：生成 <build>/.../<target>Profile.qrc（前缀 /<target>/profile）编进客户库，
#    UI_CUSTOMER_PROFILE 定义为 ":/<target>/profile/ui.json"，作为 ProfileLocator 的兜底；
#    静态链接时需在 CustomerModule.cpp 调用 Q_INIT_RESOURCE(<target>Profile)。
# 2. 外置：在目标上记录 UI_PROFILE_DIR，由 UiDeployProfile() 在 POST_BUILD
#    把 ui.json 与 assets/ 复制到 <exe>/profile/，现场改配置无需重新编译。
# 源码路径不编进二进制，about.logo 等相对路径按 profile 所在目录解析。

function(UiCustomerProfile target)
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_profile "${_dir}/ui.json")
    if(NOT EXISTS "${_profile}")
        message(FATAL_ERROR "UiCustomerProfile: profile not found: ${_profile}")
    endif()

    set(_prefix "/${target}/profile")
    set(_entries "        <file alias=\"ui.json\">${_profile}</file>\n")
    file(GLOB_RECURSE _assets CONFIGURE_DEPENDS "${_dir}/assets/*")
    list(SORT _assets)
    foreach(_asset IN LISTS _assets)
        file(RELATIVE_PATH _rel "${_dir}" "${_asset}")
        string(APPEND _entries "        <file alias=\"${_rel}\">${_asset}</file>\n")
    endforeach()

    # file(CONFIGURE) 仅在内容变化时改写文件，避免每次配置都触发 rcc 重编。
    set(_qrc "${CMAKE_CURRENT_BINARY_DIR}/${target}Profile.qrc")
    file(CONFIGURE OUTPUT "${_qrc}" CONTENT
"<!-- 由 cmake/UiCustomerProfile.cmake 生成，不要手改 -->
<RCC>
    <qresource prefix=\"${_prefix}\">
${_entries}    </qresource>
</RCC>
" @ONLY)

    target_sources(${target} PRIVATE "${_qrc}")
    target_compile_definitions(${target} PRIVATE
        UI_CUSTOMER_PROFILE=":${_prefix}/ui.json")
    set_target_properties(${target} PROPERTIES UI_PROFILE_DIR "${_dir}")
endfunction()
