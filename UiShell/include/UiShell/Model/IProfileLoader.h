#pragma once

#include <QByteArray>
#include <QList>

// 以下类型定义在 src/Model/ 内部头中。使用本接口的代码需要把 src/ 加入 include 路径
// （测试 target 已显式添加；UiShell 链接 UiShellModel 时也能隐式继承）。
#include "Model/Json/Jsonc.h"
#include "Model/Profile/ProfileLoadResult.h"
#include "Model/Profile/ProfileModel.h"
#include "UiShell/Contract/Diagnostic.h"

namespace AA
{
    // Profile 加载/校验/JSONC 预处理统一入口；具体实现类留在 src/Model/ 内部。
    class IProfileLoader
    {
    public:
        virtual ~IProfileLoader() = default;

    public:
        // 解析 ui.json 为加载结果；model 内无 Error 诊断时才有值。
        virtual ProfileLoadResult loadFromJson(const QByteArray& utf8Json) = 0;

        // 对已解析的 model 做跨字段校验（唯一性/引用有效性/上限）。
        virtual QList<Diagnostic> validate(const ProfileModel& model) = 0;

        // 原地替换 JSONC 中的 // / /* */ 注释为空格，保持字节偏移一致。
        virtual JsoncStripResult stripComments(const QByteArray& input) const = 0;

        // 将字节偏移换算为 1 基行号/列号（QJsonParseError 用）。
        virtual void lineColumn(const QByteArray& utf8, int offset,
            int* line, int* column) const = 0;

        // 统一用 destroy() 释放，避免跨 DLL 边界 delete 不匹配。
        virtual void destroy() = 0;
    };

    // 静态库 UiShellModel 内实现；调用方链接 UiShellModel 后可见。
    IProfileLoader* createProfileLoader();
}  // namespace AA
