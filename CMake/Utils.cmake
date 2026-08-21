if(NOT DEFINED SANYI_COMPANY_NAME)
    set(SANYI_COMPANY_NAME "SanYi Technology")
endif()

if(NOT DEFINED SANYI_PRODUCT_NAME)
    set(SANYI_PRODUCT_NAME "SanYi CAD")
endif()

if(NOT DEFINED SANYI_LEGAL_COPYRIGHT)
    set(SANYI_LEGAL_COPYRIGHT "Copyright (C) 2026 SanYi Technology. All rights reserved.")
endif()

set(SANYI_UTILS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Directory containing Sanyi utility modules")

function(sanyi_add_version_info target target_name description)
    if(MSVC)
        set(_FILE_DESCRIPTION "${description}")
        set(_INTERNAL_NAME "${target_name}")
        set(_ORIGINAL_FILENAME "${target_name}.dll")
        get_target_property(_target_type ${target} TYPE)
        if(_target_type STREQUAL "EXECUTABLE")
            set(_ORIGINAL_FILENAME "${target_name}.exe")
        endif()
        set(_version_rc_in "${SANYI_UTILS_DIR}/version.rc.in")
        set(_version_rc_out "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_version.rc")
        if(NOT EXISTS "${_version_rc_in}")
            message(FATAL_ERROR "[sanyi_add_version_info] version.rc.in NOT FOUND: ${_version_rc_in}")
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        # CMake 4.3 configure_file 对带 BOM 的文件可能存在问题，
        # 改用 file(READ) + string(CONFIGURE) + file(WRITE) 替代
        file(READ "${_version_rc_in}" _version_rc_content)
        string(CONFIGURE "${_version_rc_content}" _version_rc_configured @ONLY)
        file(WRITE "${_version_rc_out}" "${_version_rc_configured}")
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_version.rc"
        )
    endif()
endfunction()

function(sanyi_deploy_qt_dlls target)
    if(WIN32 AND QT_VERSION_MAJOR)
        find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Core)
        if(Qt${QT_VERSION_MAJOR}_FOUND)
            get_target_property(_qt_core_location Qt${QT_VERSION_MAJOR}::Core LOCATION)
            get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
            get_filename_component(_qt_bin_dir "${_qt_bin_dir}" DIRECTORY)
            set(_qt_bin_dir "${_qt_bin_dir}/bin")

            get_target_property(_target_type ${target} TYPE)
            if(_target_type STREQUAL "EXECUTABLE")
                set(_output_dir "$<TARGET_FILE_DIR:${target}>")
            else()
                set(_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            endif()

            if(EXISTS "${_qt_bin_dir}/windeployqt.exe")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND "${_qt_bin_dir}/windeployqt.exe"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        --no-quick
                        --no-quick-import
                        --dir "${_output_dir}"
                        "$<TARGET_FILE:${target}>"
                    COMMENT "Deploying Qt DLLs for ${target}"
                    VERBATIM
                )
            else()
                message(WARNING "windeployqt not found at ${_qt_bin_dir}/windeployqt.exe")
            endif()
        endif()
    endif()
endfunction()

function(sanyi_add_debug_symbols target)
    if(MSVC)
        # MSVC: 仅设置调试符号选项，不尝试在configure时复制PDB文件
        # PDB文件在build时间生成，configure时引用会失败
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/Zi>
            $<$<CONFIG:RelWithDebInfo>:/Zi>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/DEBUG>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            $<$<CONFIG:RelWithDebInfo>:/DEBUG>
        )
        message(STATUS "MSVC debug symbols enabled for ${target} (PDB copy deferred to build time)")
    elseif(APPLE)
        target_compile_options(${target} PRIVATE -g)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND dsymutil "$<TARGET_FILE:${target}>" -o "$<TARGET_FILE_DIR:${target}>/symbols/${target}.dSYM"
            COMMENT "Generating dSYM symbols for ${target}"
        )
    elseif(UNIX)
        target_compile_options(${target} PRIVATE -g)
        set(_debug_output "$<TARGET_FILE_DIR:${target}>/symbols/${target}.debug")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/symbols"
            COMMAND objcopy --only-keep-debug "$<TARGET_FILE:${target}>" "${_debug_output}"
            COMMAND strip --strip-debug "$<TARGET_FILE:${target}>"
            COMMAND objcopy --add-gnu-debuglink="${_debug_output}" "$<TARGET_FILE:${target}>"
            COMMENT "Separating debug symbols for ${target}"
        )
    endif()
endfunction()

function(sanyi_glob_sources OUT_VAR)
    set(options)
    set(oneValueArgs BASE_DIR)
    set(multiValueArgs DIRS EXCLUDE_PATTERNS)
    cmake_parse_arguments(SANYI_GLOB "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SANYI_GLOB_BASE_DIR)
        set(SANYI_GLOB_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    set(_sources "")
    foreach(dir ${SANYI_GLOB_DIRS})
        file(GLOB _dir_files CONFIGURE_DEPENDS "${SANYI_GLOB_BASE_DIR}/${dir}/*.cpp")
        list(APPEND _sources ${_dir_files})
    endforeach()

    if(SANYI_GLOB_EXCLUDE_PATTERNS)
        foreach(pattern ${SANYI_GLOB_EXCLUDE_PATTERNS})
            list(FILTER _sources EXCLUDE REGEX "${pattern}")
        endforeach()
    endif()

    if(DEFINED SANYI_SOURCE_EXCLUDE_REGEX)
        list(FILTER _sources EXCLUDE REGEX "${SANYI_SOURCE_EXCLUDE_REGEX}")
    endif()

    set(${OUT_VAR} "${_sources}" PARENT_SCOPE)
endfunction()

function(sanyi_glob_headers OUT_VAR)
    set(options)
    set(oneValueArgs BASE_DIR)
    set(multiValueArgs DIRS EXCLUDE_PATTERNS)
    cmake_parse_arguments(SANYI_GLOB "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SANYI_GLOB_BASE_DIR)
        set(SANYI_GLOB_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    set(_headers "")
    foreach(dir ${SANYI_GLOB_DIRS})
        file(GLOB _dir_files CONFIGURE_DEPENDS "${SANYI_GLOB_BASE_DIR}/${dir}/*.h")
        list(APPEND _headers ${_dir_files})
    endforeach()

    if(SANYI_GLOB_EXCLUDE_PATTERNS)
        foreach(pattern ${SANYI_GLOB_EXCLUDE_PATTERNS})
            list(FILTER _headers EXCLUDE REGEX "${pattern}")
        endforeach()
    endif()

    if(DEFINED SANYI_HEADER_EXCLUDE_REGEX)
        list(FILTER _headers EXCLUDE REGEX "${SANYI_HEADER_EXCLUDE_REGEX}")
    endif()

    set(${OUT_VAR} "${_headers}" PARENT_SCOPE)
endfunction()

function(sanyi_add_module_sources OUT_VAR)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs DIRS)
    cmake_parse_arguments(SANYI_MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(_sources "")
    foreach(dir ${SANYI_MOD_DIRS})
        sanyi_glob_sources(_dir_src BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Src" DIRS "${dir}")
        list(APPEND _sources ${_dir_src})
        sanyi_glob_headers(_dir_hdr BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Src" DIRS "${dir}")
        list(APPEND _sources ${_dir_hdr})
    endforeach()

    set(${OUT_VAR} "${_sources}" PARENT_SCOPE)
endfunction()

function(sanyi_find_qt)
    if(Qt_INSTALL_DIR)
        list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS ${ARGN})
    foreach(comp ${ARGN})
        list(APPEND QT_LIBRARIES Qt6::${comp})
    endforeach()
    set(QT_LIBRARIES "${QT_LIBRARIES}" PARENT_SCOPE)
endfunction()

function(sanyi_add_shared_library target)
    set(options AUTOMOC AUTOUIC AUTORCC UNITY_BUILD)
    set(oneValueArgs FOLDER DESCRIPTION EXPORT_MACRO VERSION_MAJOR VERSION_MINOR VERSION_PATCH)
    set(multiValueArgs SOURCES HEADERS PUBLIC_INCLUDE_DIRS PRIVATE_INCLUDE_DIRS 
                       COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES QT_COMPONENTS)
    cmake_parse_arguments(SANYI_SHLIB "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(SANYI_SHLIB_VERSION_MAJOR)
        set(_version "${SANYI_SHLIB_VERSION_MAJOR}")
        if(DEFINED SANYI_SHLIB_VERSION_MINOR)
            set(_version "${_version}.${SANYI_SHLIB_VERSION_MINOR}")
            if(DEFINED SANYI_SHLIB_VERSION_PATCH)
                set(_version "${_version}.${SANYI_SHLIB_VERSION_PATCH}")
            endif()
        endif()
        set(PROJECT_VERSION "${_version}")
        # version.rc.in 使用 @PROJECT_VERSION_MAJOR@ 等分量变量，
        # 必须同步设置，否则 FILEVERSION 会生成空的 ,,,0
        set(PROJECT_VERSION_MAJOR "${SANYI_SHLIB_VERSION_MAJOR}")
        set(PROJECT_VERSION_MINOR "${SANYI_SHLIB_VERSION_MINOR}")
        set(PROJECT_VERSION_PATCH "${SANYI_SHLIB_VERSION_PATCH}")
    endif()

    add_library(${target} SHARED
        ${SANYI_SHLIB_SOURCES}
        ${SANYI_SHLIB_HEADERS}
    )

    set(_internal_files "")
    set(_external_files "")
    foreach(file ${SANYI_SHLIB_SOURCES} ${SANYI_SHLIB_HEADERS})
        # CMake 4.x 下 file(RELATIVE_PATH) 要求 file 为绝对路径；
        # 归一化相对路径（如 Renderx 内联源列表）避免 configure 失败
        if(NOT IS_ABSOLUTE "${file}")
            get_filename_component(file "${CMAKE_CURRENT_SOURCE_DIR}/${file}" ABSOLUTE)
        endif()
        file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}" "${file}")
        if(rel_path MATCHES "^\\.\\.")
            list(APPEND _external_files "${file}")
        else()
            list(APPEND _internal_files "${file}")
        endif()
    endforeach()

    if(_internal_files)
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_internal_files})
    endif()
    if(_external_files)
        source_group("External" FILES ${_external_files})
    endif()

    if(SANYI_SHLIB_PUBLIC_INCLUDE_DIRS)
        target_include_directories(${target} PUBLIC ${SANYI_SHLIB_PUBLIC_INCLUDE_DIRS})
    endif()
    if(SANYI_SHLIB_PRIVATE_INCLUDE_DIRS)
        target_include_directories(${target} PRIVATE ${SANYI_SHLIB_PRIVATE_INCLUDE_DIRS})
    endif()

    target_compile_features(${target} PUBLIC cxx_std_17)

    if(SANYI_SHLIB_EXPORT_MACRO)
        target_compile_definitions(${target} PRIVATE ${SANYI_SHLIB_EXPORT_MACRO})
    endif()
    if(SANYI_SHLIB_COMPILE_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${SANYI_SHLIB_COMPILE_DEFINITIONS})
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /FS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(SANYI_SHLIB_COMPILE_OPTIONS)
        target_compile_options(${target} PRIVATE ${SANYI_SHLIB_COMPILE_OPTIONS})
    endif()

    set(_target_properties
        FOLDER "${SANYI_SHLIB_FOLDER}"
        DEBUG_POSTFIX "_d"
        VERSION "${PROJECT_VERSION}"
    )
    # 未显式指定版本分量时留空，避免 SOVERSION 展开为空值导致参数奇偶错位
    if(SANYI_SHLIB_VERSION_MAJOR)
        list(APPEND _target_properties SOVERSION "${SANYI_SHLIB_VERSION_MAJOR}")
    endif()

    # MSVC 运行库：统一动态链接 CRT（/MD 发布, /MDd 调试）。
    # 注意：不能在 configure 阶段依赖父作用域 CMAKE_MSVC_RUNTIME_LIBRARY，
    # 子目录内的 project() 会重置该变量导致 RuntimeLibrary 为空，
    # 从而出现 LNK2001: unresolved external symbol _DllMainCRTStartup。
    # 这里按目标显式设置 MSVC_RUNTIME_LIBRARY 属性，确保所有模块一致。
    if(MSVC)
        list(APPEND _target_properties MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    if(SANYI_SHLIB_AUTOMOC)
        list(APPEND _target_properties AUTOMOC ON)
    endif()
    if(SANYI_SHLIB_AUTOUIC)
        list(APPEND _target_properties AUTOUIC ON)
    endif()
    if(SANYI_SHLIB_AUTORCC)
        list(APPEND _target_properties AUTORCC ON)
    endif()
    if(SANYI_SHLIB_UNITY_BUILD)
        list(APPEND _target_properties UNITY_BUILD ON)
    endif()

    set_target_properties(${target} PROPERTIES ${_target_properties})

    if(SANYI_SHLIB_QT_COMPONENTS)
        sanyi_find_qt(${SANYI_SHLIB_QT_COMPONENTS})
        target_link_libraries(${target} PUBLIC ${QT_LIBRARIES})
    endif()

    if(SANYI_SHLIB_LINK_LIBRARIES)
        target_link_libraries(${target} ${SANYI_SHLIB_LINK_LIBRARIES})
    endif()

    sanyi_add_version_info(${target} "${target}" "${SANYI_SHLIB_DESCRIPTION}")

    sanyi_add_debug_symbols(${target})

    message(STATUS "[${target}]")
    message(STATUS "  Version: ${PROJECT_VERSION}")
    message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}(${CMAKE_SYSTEM_PROCESSOR})")
    message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS "----------------------------------------")
endfunction()
