#pragma once

#include <QString>

/**
 * @file UiLabelLocalizer.h
 * @brief 配置文案本地化工具（从 UiLayoutBuilder::localizedLabel 抽出）
 *
 * 翻译 JSON 配置中的英文文案（菜单/右键菜单/工具栏/Dock 标题共用）。
 * 依次查询 WorkbenchMenuManager → MainWindow → UiLayoutBuilder 上下文，
 * 取第一个命中的译文；均无译文时返回 fallbackId（非空）或原文。
 *
 * 抽为自由函数是为了让消费者（如 WorkbenchLayoutManager）不必包含
 * 具体的 UiLayoutBuilder 头。
 */
QString uiLocalizedLabel(const QString& label, const QString& fallbackId = QString());
