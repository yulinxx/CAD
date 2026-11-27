#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace RD::samples
{
    // 示例算法的参数：与界面无关，对话框只负责收集它，命令拿它调用算法。
    // JSON 键与客户代码绑定的 params、DialogResult.data 一致：
    //   target（被开方数）/ maxIterations / tolerance / method（"newton" | "bisection"）
    struct SampleParams
    {
        double target = 2.0;
        int maxIterations = 20;
        double tolerance = 1e-6;
        QString method = QStringLiteral("newton");

        // 缺省键取默认值；越界值夹到合法范围；未知 method 回退 newton。
        static SampleParams fromJson(const QJsonObject& json);
        QJsonObject toJson() const;
    };

    // 参数的合法范围：对话框控件与 fromJson 共用，保证两边一致。
    namespace SampleLimits
    {
        inline constexpr double kMaxTarget = 1e9;
        inline constexpr int kMinIterations = 1;
        inline constexpr int kMaxIterations = 10000;
        inline constexpr double kMinTolerance = 1e-12;
        inline constexpr double kMaxTolerance = 1.0;
    }  // namespace SampleLimits

    struct SampleResult
    {
        bool valid = true;
        QString error;  // valid 为 false 时的原因（英文，供日志与提示）。
        double value = 0.0;
        int iterations = 0;
        bool converged = false;
    };

    // 支持的 method id，顺序即对话框下拉顺序。
    QStringList sampleMethods();

    // 示例算法：求 sqrt(target)。纯函数，不依赖任何窗口，可单独单测。
    SampleResult solveSquareRoot(const SampleParams& params);
}  // namespace RD::samples
