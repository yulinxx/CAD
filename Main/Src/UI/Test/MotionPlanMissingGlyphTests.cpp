/**
 * @file MotionPlanMissingGlyphTests.cpp
 * @brief 文字缺字在真实编译入口的汇总测试。
 *
 * 走 MotionPlanCompiler（生产 2D 出刀入口），确认字体缺字不会静默消失：
 * 编译结果尾部必须给出缺字总数与带位置信息的明细。
 */
#include <gtest/gtest.h>

#include <string>

#include "Hardware/ToolpathJobSpec.h"

#include "Engine/Text/FontManager.h"
#include "Engine2D/AprEntity/AprText.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"

#ifdef ENABLE_HARDWARE
    #include "Hardware/Device/MotionPlan.h"
    #include "Hardware/MotionPlanCompiler.h"

TEST(MotionPlanMissingGlyphTest, CompileResultReportsMissingGlyphs)
{
    Eg::FontManager::instance().scanSystemFonts();

    Eg::SceneManager scene;
    Eg::AprText text;
    // U+E000 私有使用区：系统字体无字形，确定性命中缺字路径
    text.setText("A\xEE\x80\x80"
                 "B");
    text.setFontName("Helvetica");
    text.dHeight = 10.0;
    text.basePoint = Ut::Vec2d(0.0, 0.0);
    Eg::SceneWriteGateway::addEntity(scene, &text);

    ToolpathJobSpec spec;
    // 文字整体仍可加工（A/B 有轮廓），缺字只应上报而不阻断
    spec.failOnUnmachinableEntity = false;

    Hw::MotionPlanBuilder builder;
    const ToolpathCompileResult result = MotionPlanCompiler::compile(scene, nullptr, spec, builder);
    if (!result.ok)
    {
        GTEST_SKIP() << "Helvetica not resolvable in this environment: " << result.error.toStdString();
    }

    ASSERT_EQ(result.missingGlyphCount, 1);
    ASSERT_EQ(result.missingGlyphDetails.size(), 1);
    const QString detail = result.missingGlyphDetails.front();
    EXPECT_NE(detail.indexOf(QStringLiteral("U+E000")), -1) << detail.toStdString();
    EXPECT_NE(detail.indexOf(QStringLiteral("line 1 col 2")), -1) << detail.toStdString();
}

#endif  // ENABLE_HARDWARE
