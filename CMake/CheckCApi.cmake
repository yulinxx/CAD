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
    set(_c_api_headers
        "${SANYI_ROOT}/CrashHandler/CrashHandler/Include/CrashHandler/CrashHandlerDLL.h"
        "${SANYI_ROOT}/License/License/Include/License/LicenseDLL.h"
        "${SANYI_LOG_DIR}/Include/Log/SyLogger.h"
        "${SANYI_ROOT}/Engraving/Engraving/Include/Engraving/EngravingCAPI.h"
        "${SANYI_ROOT}/GeoModelCore/GeoModelCore/Include/GeoModelCore/GeoModelDLL.h"
        "${SANYI_VISION_DIR}/Include/Vision/VisionAPI.h"
        "${SANYI_NESTING_DIR}/Include/Nesting/NestingApi.h"
        "${SANYI_RENDERX_DIR}/include/render/renderx.h"
    )

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
