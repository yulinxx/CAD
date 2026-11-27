#pragma once

#include <QByteArray>

#include "Model/Json/Jsonc.h"

namespace RD
{
    // BOM 与 注释 清理
    JsoncStripResult stripJsoncComments(const QByteArray& input);

    // 计算 UTF-8 字节偏移对应的 1 基行号/列号（按字节计列，足够用于报错定位）。
    // line/column 任一可为空；offset 超出范围时按边界截断。
    void jsoncLineColumn(const QByteArray& utf8, qsizetype offset, int* line, int* column);
}  // namespace RD
