#include "UiDialogs/SampleSolver.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace RD::samples
{
    QStringList sampleMethods()
    {
        return { QStringLiteral("newton"), QStringLiteral("bisection") };
    }

    SampleParams SampleParams::fromJson(const QJsonObject& json)
    {
        SampleParams params;

        if (json.contains(QStringLiteral("target")))
        {
            params.target = std::min(std::max(
                json.value(QStringLiteral("target")).toDouble(params.target), 0.0), SampleLimits::kMaxTarget);
        }
        if (json.contains(QStringLiteral("maxIterations")))
        {
            params.maxIterations = std::min(std::max(json.value(QStringLiteral("maxIterations")).toInt(params.maxIterations),
                SampleLimits::kMinIterations), SampleLimits::kMaxIterations);
        }
        if (json.contains(QStringLiteral("tolerance")))
        {
            params.tolerance = std::min(std::max(json.value(QStringLiteral("tolerance")).toDouble(params.tolerance),
                SampleLimits::kMinTolerance), SampleLimits::kMaxTolerance);
        }

        const QString method = json.value(QStringLiteral("method")).toString();
        if (sampleMethods().contains(method))
            params.method = method;

        return params;
    }

    QJsonObject SampleParams::toJson() const
    {
        QJsonObject json;
        json.insert(QStringLiteral("target"), target);
        json.insert(QStringLiteral("maxIterations"), maxIterations);
        json.insert(QStringLiteral("tolerance"), tolerance);
        json.insert(QStringLiteral("method"), method);
        return json;
    }

    SampleResult solveSquareRoot(const SampleParams& params)
    {
        SampleResult result;

        // !(x >= 0) 同时挡住负数与 NaN。
        if (!(params.target >= 0.0))
        {
            result.valid = false;
            result.error = QStringLiteral("target must not be negative");
            return result;
        }
        if (params.target == 0.0)
        {
            result.converged = true;
            return result;
        }

        const int maxIterations = std::max(SampleLimits::kMinIterations, params.maxIterations);
        const double tolerance = std::max(params.tolerance, SampleLimits::kMinTolerance);

        if (params.method == QLatin1String("bisection"))
        {
            double low = 0.0;
            double high = std::max(1.0, params.target);
            while (result.iterations < maxIterations)
            {
                const double mid = 0.5 * (low + high);
                ++result.iterations;
                if (mid * mid > params.target)
                    high = mid;
                else
                    low = mid;

                if (high - low < tolerance)
                {
                    result.converged = true;
                    break;
                }
            }
            result.value = 0.5 * (low + high);
            return result;
        }

        // 牛顿迭代：x(n+1) = (x(n) + target / x(n)) / 2
        double x = std::max(1.0, params.target);
        while (result.iterations < maxIterations)
        {
            const double next = 0.5 * (x + params.target / x);
            ++result.iterations;
            const double delta = std::fabs(next - x);
            x = next;
            if (delta < tolerance)
            {
                result.converged = true;
                break;
            }
        }
        result.value = x;
        return result;
    }
}  // namespace RD::samples