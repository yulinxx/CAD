/**
 * @file AppInitializer.cpp
 * @brief 应用程序初始化器实现
 */

#include "AppInitializer.h"

#include <QApplication>
#include <QFont>

void AppInitializer::initialize()
{
    // 设置应用程序字体
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
}