/**
 * @file FrameOverlayTextTests.cpp
 * @brief 视口右下角 FPS/CPU/GPU 文本的格式契约
 *
 * 只测纯格式化，不建窗口、不碰 GPU：口径（小数位、分隔、不可用占位符）
 * 一旦被改动，这里会直接失败，而不是等到肉眼比对两个视口的 HUD。
 */

#include <gtest/gtest.h>

#include "UI/FpsOverlayMeter.h"

TEST(FrameOverlayText, FormatsFpsCpuAndGpuOnOneLine)
{
    char buf[128];
    Ui::formatFrameText(59.94f, 3.421, 2.096, "FPS", buf, sizeof(buf));
    EXPECT_STREQ("FPS: 59.9  CPU: 3.42 ms  GPU: 2.10 ms", buf);
}

TEST(FrameOverlayText, UnavailableValuesRenderAsPlaceholders)
{
    char buf[128];
    Ui::formatFrameText(-1.0f, 0.0, 0.0, "FPS", buf, sizeof(buf));
    EXPECT_STREQ("FPS: --  CPU: --  GPU: --", buf);
}

TEST(FrameOverlayText, LocalizedTitleFitsAndIsPreserved)
{
    char buf[128];
    Ui::formatFrameText(60.0f, 1.0, 1.0, "帧率", buf, sizeof(buf));
    EXPECT_STREQ("帧率: 60.0  CPU: 1.00 ms  GPU: 1.00 ms", buf);
}

TEST(FrameOverlayText, NullBufferIsIgnored)
{
    // 防御性约定：缓冲无效时直接返回，不能因诊断显示把渲染线程弄崩
    Ui::formatFrameText(60.0f, 1.0, 1.0, "FPS", nullptr, 0);
}
