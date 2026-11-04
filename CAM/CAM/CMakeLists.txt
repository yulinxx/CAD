# ============================================================================
# CAM - 工艺编译层（M0）
# ============================================================================
#
# 输入 ：ToolpathJobInput（中性 C++ 值类型，自持有数据；属 C++ 内部 ABI）
# 输出 ：CamPlan（有序工序 + 路径 + 统计）+ CamResult（错误码 + 统计）
# ABI  ：C++ 内部 ABI（公开头暴露 STL 容器，使用方必须同工具链构建）
# 依赖 ：Utility PUBLIC（Ut::Vec2d）、Log PRIVATE（内部日志）
# 不依赖：Engine2D / Engraving / Hardware / UI 模块
#
# 目录结构（架构规划详见 Docs/CAM层架构规划.md）：
#   Include/CAM/          —— 公开头
#   Src/CamCore.cpp       —— 主编译入口（M0）
#   Src/GeometryTransform/ —— M1 落地：割缝补偿 / 引线 / 穿孔 / 共边 / 拐角
#   Src/Order/            —— M1 落地：最近邻 / 2-opt 空移优化
#   Src/Process/          —— M1 落地：功率曲线 / 参数整合 / 斜坡下降
#   Src/Emit/             —— M2 落地：MotionPlanBuilder 发射
#   Src/Validate/         —— M1 落地：软限位预检
#   Src/Estimate/         —— M1 落地：时间预估
#   Test/                 —— 单元测试
#
# ============================================================================

set(LIB_NAME  "CAM")
set(LIB_FOLDER "CAM")
set(LIB_DESCRIPTION
    "CAM layer: process compilation from neutral toolpath geometry to ordered motion plan with statistics")
set(LIB_EXPORT_MACRO "CAM_EXPORTS")

sanyi_add_shared_library(${LIB_NAME}
    FOLDER        "${LIB_FOLDER}"
    DESCRIPTION   "${LIB_DESCRIPTION}"
    EXPORT_MACRO  "${LIB_EXPORT_MACRO}"

    SOURCES
        Src/CamCore.cpp

    HEADERS
        Include/CAM/CAMExport.h
        Include/CAM/ToolpathJobInput.h
        Include/CAM/CamPlan.h
        Include/CAM/CamContext.h
        Include/CAM/CamCore.h

    PUBLIC_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/Include

    PUBLIC_LINK_LIBRARIES
        Utility

    PRIVATE_LINK_LIBRARIES
        Log
)

# ============================================================================
#  测试
# ============================================================================
if(BUILD_ALL_TESTS)
    add_subdirectory(Test)
endif()
