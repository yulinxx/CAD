# ============================================================================
# C ABI 版本查询校验
# ----------------------------------------------------------------------------
# 约定见 Docs/01-当前架构/C-ABI风格约定.md：
#   每个对外 C ABI 模块至少提供
#     - <Module>_GetVersionString()  （或等价命名，含 GetVersionString）
#     - <Module>_GetVersion() / <Module>_GetAbiVersion() 之一
#
# 这里维护的是"含 C ABI 函数声明"的头（不是仅含导出宏的 *API.h）。
# 新增 C ABI 模块时，把其函数头登记到下面的列表。
# ============================================================================

function(sanyi_check_c_api_version)
    set(_c_api_headers "")

    # 无条件校验的模块（根 CMakeLists.txt 直接 add_subdirectory，无 BUILD_xxx 开关）
    list(APPEND _c_api_headers
        "${SANYI_ROOT}/Licensing/License/Include/License/LicenseDLL.h"
        "${SANYI_LOG_DIR}/Include/Log/SyLogger.h"
    )

    # 以下均由 BUILD_<MODULE> 开关控制，只有实际构建的模块才纳入校验
    if(BUILD_CRASHHANDLER)
        list(APPEND _c_api_headers
            "${SANYI_ROOT}/CrashHandler/CrashHandler/Include/CrashHandler/CrashHandlerDLL.h"
        )
    endif()
    if(BUILD_ENGRAVING)
        list(APPEND _c_api_headers
            "${SANYI_ROOT}/Engraving/Engraving/Include/Engraving/EngravingCAPI.h"
        )
    endif()
    if(BUILD_GEOMODELCORE)
        list(APPEND _c_api_headers
            "${SANYI_GEOMODEL_DIR}/Include/GeoModelCore/GeoModelDLL.h"
        )
    endif()
    if(BUILD_VISION)
        list(APPEND _c_api_headers
            "${SANYI_VISION_DIR}/Include/Vision/VisionAPI.h"
        )
    endif()
    if(BUILD_RASTER)
        list(APPEND _c_api_headers
            "${SANYI_RASTER_DIR}/Include/Raster/RasterApi.h"
        )
    endif()
    if(BUILD_NESTING)
        list(APPEND _c_api_headers
            "${SANYI_NESTING_DIR}/Include/Nesting/NestingApi.h"
        )
    endif()
    if(BUILD_RENDERX)
        list(APPEND _c_api_headers
            "${SANYI_RENDERX_DIR}/include/render/renderx.h"
        )
    endif()

    set(_problems "")
    foreach(_h IN LISTS _c_api_headers)
        if(NOT EXISTS "${_h}")
            list(APPEND _problems "找不到 C ABI 头：${_h}")
            continue()
        endif()
        file(READ "${_h}" _content)
        if(NOT _content MATCHES "GetVersionString")
            list(APPEND _problems "${_h}: 缺 *_GetVersionString()")
        endif()
        if(NOT _content MATCHES "GetVersion|GetAbiVersion")
            list(APPEND _problems "${_h}: 缺 *_GetVersion() / *_GetAbiVersion()")
        endif()
    endforeach()

    if(_problems)
        string(REPLACE ";" "\n    - " _msg "${_problems}")
        message(FATAL_ERROR
            "[sanyi_check_c_api_version] C ABI 版本查询不合规：\n    - ${_msg}\n"
            "  约定见 Docs/01-当前架构/C-ABI风格约定.md")
    endif()

    message(STATUS "[SanYi] C ABI 版本查询校验: OK")
endfunction()
