#pragma once

#include <QByteArray>
#include <QString>

namespace RD
{
    // JSONC（带注释的 JSON）预处理结果
    //
    // - 注释字符用空格原地替换，不删除，保证字节偏移与行号与原文件一致；
    // - 正确跳过字符串字面量内部的 // 与 /* */，并处理 \" 转义；
    // - 未闭合的块注释返回错误与起始行号，而不是静默吞掉剩余内容。
    struct JsoncStripResult
    {
        QByteArray json;
        bool ok = true;
        int errorLine = 0;  // 1 基行号，仅在 ok 为 false 时有意义
        QString error;
    };
}  // namespace RD
