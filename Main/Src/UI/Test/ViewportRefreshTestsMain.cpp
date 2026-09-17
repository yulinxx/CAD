/**
 * @file ViewportRefreshTestsMain.cpp
 * @brief ViewportRefreshTests 的专用入口（自带 QApplication）
 *
 * 为什么需要它：这 56 条用例测的是 2D 刷新契约，而契约里的节流是 `QTimer` ——
 * 没有 QApplication 就没有事件分发器，`QTimer::start()` 只会打印
 * 「current thread's event dispatcher has already been destroyed」然后什么都不做，
 * 于是测试跑在一个**节流不生效**的退化环境里。
 *
 * 为什么不用 GTest::gtest_main：那是个裸 main，没有地方建 QApplication。
 * 也不用 gtest 的全局环境（像 MainTests 那样）——这里是独立目标，一个 main 最直接。
 *
 * 注意：本文件**不能**出现在 MainTests 的源列表里（会多出一个 main），
 * Main/CMakeLists.txt 的 MAIN_TEST_SOURCES 已按文件名把它排除。
 */
#include <gtest/gtest.h>

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}