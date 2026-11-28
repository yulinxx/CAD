#pragma once

#include <QDialog>
#include <QJsonObject>

#include "UiDialogs/SampleSolver.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

namespace AA::samples
{
    // 默认参数对话框：纯界面，只把 params 摆成表单、把表单读回 SampleParams，不运行算法。
    // 控件对象名稳定（sample.target / sample.maxIterations / sample.tolerance / sample.method），供测试与 QSS 使用。
    class SampleParamsDialog : public QDialog
    {
    public:
        explicit SampleParamsDialog(const QJsonObject& params, QWidget* parent = nullptr);

        SampleParams values() const;

    private:
        QDoubleSpinBox* m_target = nullptr;
        QSpinBox* m_maxIterations = nullptr;
        QDoubleSpinBox* m_tolerance = nullptr;
        QComboBox* m_method = nullptr;
    };
}  // namespace AA::samples
