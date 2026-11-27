# =============================================================================
# 2D 算法分层依赖门禁（P0）
#
# 规则与例外清单见 Tools/check_2d_layer_deps.py 文件头，
# 设计依据见 Docs/06-算法/2D/00-算法框架分层设计.md §1.1 / §6。
#
# 默认硬失败：算法层一旦重新长出对 UI/场景/其它模块的依赖，configure 立即失败。
# 紧急放行用 -DAPR_STRICT_2D_LAYERS=OFF（只告警，CI 不允许关）。
# =============================================================================

option(APR_STRICT_2D_LAYERS "Fail configure on 2D algorithm layer dependency violations" ON)

function(apr_check_2d_layer_deps)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "[2D-layer-deps] Python3 interpreter not found: guard skipped")
        return()
    endif()

    set(_script "${APR_ROOT}/Tools/check_2d_layer_deps.py")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_script}" "${APR_ROOT}"
        WORKING_DIRECTORY "${APR_ROOT}"
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _rc
    )
    if(_out)
        message(STATUS "${_out}")
    endif()
    if(_err)
        message(STATUS "${_err}")
    endif()
    if(NOT _rc EQUAL 0)
        if(APR_STRICT_2D_LAYERS)
            message(FATAL_ERROR "[2D-layer-deps] layer dependency violations detected (see above)")
        else()
            message(WARNING "[2D-layer-deps] layer dependency violations detected (non-strict, see above)")
        endif()
    endif()
endfunction()
