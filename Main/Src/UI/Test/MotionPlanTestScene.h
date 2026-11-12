#pragma once

/**
 * @file MotionPlanTestScene.h
 * @brief MotionPlanCompiler 生产链测试共用的场景构造 / 编译辅助。
 *
 * 为什么单独成文件：MainTests 用 unity 构建，多个 *Tests.cpp 会并入同一个
 * 翻译单元。若各文件都在匿名命名空间里各写一份同名辅助，unity 合并后直接
 * 编译失败（重定义），而不是链接期才发现。这里只保留一份。
 *
 * 依赖 ENABLE_HARDWARE：调用方需在该宏的 #ifdef 内 include。
 */

#include <vector>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprPolygon.h"

#include "Hardware/Device/MotionPlan.h"
#include "Hardware/MotionPlanCompiler.h"

namespace MotionTest
{
    inline Eg::AprEntity* addPolygon(Eg::SceneManager& scene,
                                     const std::vector<Ut::Vec2d>& vertices)
    {
        Eg::AprPolygon poly;
        poly.setVertices(vertices);
        poly.bClosed = true;
        Eg::SceneWriteGateway::addEntity(scene, &poly);

        const std::vector<Eg::AprEntity*> all = scene.getAllEntities();
        return all.empty() ? nullptr : all.back();
    }

    inline std::vector<Ut::Vec2d> rectPoints(double x0, double y0, double x1, double y1)
    {
        return { Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y0), Ut::Vec2d(x1, y1), Ut::Vec2d(x0, y1) };
    }

    inline ToolpathCompileResult compileRect(Eg::SceneManager& scene,
                                             const ToolpathJobSpec& spec,
                                             Hw::MotionPlanBuilder& builder)
    {
        return MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    }
}  // namespace MotionTest
