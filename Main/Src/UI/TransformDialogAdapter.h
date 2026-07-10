/**
 * @file TransformDialogAdapter.h
 * @brief TransformDialog 适配器 — 通用变换参数输入层
 *
 * 这是一个轻量级适配器，将参数收集逻辑封装为 ITransformInputProvider 接口，
 * 使得 Move / Copy / Rotate / Mirror 等操作可以复用同一套参数输入逻辑。
 *
 * 设计目标：
 * - 让 Operation 不直接依赖 UI
 * - 让参数收集和参数使用分离
 * - 支持多种输入方式（对话框、鼠标交互、脚本）
 * - 支持参数验证和预览
 */
#pragma once

#include "TransformInputProvider.h"
#include <memory>

class QWidget;
class QDialog;
class QVBoxLayout;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QButtonGroup;
class QGroupBox;
class QCheckBox;
class QDialogButtonBox;
class SceneDocument2D;

class TransformDialogAdapter : public ITransformInputProvider
{
public:
    explicit TransformDialogAdapter(SceneDocument2D* document, QWidget* parent = nullptr);
    ~TransformDialogAdapter() override = default;

    /**
     * @brief 获取变换参数
     * @return 变换参数
     *
     * 这是一个阻塞调用，会等待用户输入完成
     */
    TransformParameters getParameters() override;

    /**
     * @brief 检查是否有有效的参数
     * @return true 表示有有效参数
     */
    bool hasValidParameters() const override;

    /**
     * @brief 获取当前参数（不阻塞）
     * @return 当前参数
     */
    TransformParameters currentParameters() const override;

    /**
     * @brief 设置参数变更回调
     * @param callback 参数变更时调用的回调函数
     */
    void setParametersChangedCallback(std::function<void(const TransformParameters&)> callback) override;

    /**
     * @brief 设置确认回调
     * @param callback 用户确认时调用的回调函数
     */
    void setConfirmedCallback(std::function<void(bool confirmed)> callback) override;

    /**
     * @brief 取消操作
     */
    void cancel() override;

    /**
     * @brief 获取变换类型
     * @return 变换类型
     */
    TransformType transformType() const override;

    /**
     * @brief 设置变换类型
     * @param type 变换类型
     */
    void setTransformType(TransformType type);

    /**
     * @brief 设置是否显示预览
     * @param show true 表示显示预览
     */
    void setShowPreview(bool show) { m_showPreview = show; }

    /**
     * @brief 获取是否显示预览
     */
    bool showPreview() const { return m_showPreview; }

private:
    /**
     * @brief 获取对话框标题
     */
    QString getDialogTitle() const;

    /**
     * @brief 创建对话框 UI
     */
    QDialog* createDialog();

    /**
     * @brief 添加移动输入控件
     */
    void addMoveInputs(QVBoxLayout* layout);

    /**
     * @brief 添加复制输入控件
     */
    void addCopyInputs(QVBoxLayout* layout);

    /**
     * @brief 添加旋转输入控件
     */
    void addRotateInputs(QVBoxLayout* layout);

    /**
     * @brief 添加镜像输入控件
     */
    void addMirrorInputs(QVBoxLayout* layout, QDialog* dialog);

    /**
     * @brief 添加预览控件
     */
    void addPreviewControls(QVBoxLayout* layout, QDialog* dialog);

    /**
     * @brief 从 UI 控件收集参数
     */
    TransformParameters collectParametersFromUI(QDialog* dialog);

    /**
     * @brief 验证参数是否有效
     */
    bool validateParameters(const TransformParameters& params) const;

    /**
     * @brief 更新预览
     */
    void updatePreview(const TransformParameters& params);

private:
    SceneDocument2D* m_document{ nullptr };
    QWidget* m_parent{ nullptr };
    TransformType m_transformType{ TransformType::Move };
    TransformParameters m_parameters;
    bool m_showPreview{ true };
    std::function<void(const TransformParameters&)> m_parametersChangedCallback;
    std::function<void(bool confirmed)> m_confirmedCallback;

    // UI 控件
    QDoubleSpinBox* m_moveXSpinBox{ nullptr };
    QDoubleSpinBox* m_moveYSpinBox{ nullptr };
    QSpinBox* m_copyCountSpinBox{ nullptr };
    QDoubleSpinBox* m_copySpacingXSpinBox{ nullptr };
    QDoubleSpinBox* m_copySpacingYSpinBox{ nullptr };
    QDoubleSpinBox* m_rotateAngleSpinBox{ nullptr };
    QDoubleSpinBox* m_rotateCenterXSpinBox{ nullptr };
    QDoubleSpinBox* m_rotateCenterYSpinBox{ nullptr };
    QDoubleSpinBox* m_mirrorLineX1SpinBox{ nullptr };
    QDoubleSpinBox* m_mirrorLineY1SpinBox{ nullptr };
    QDoubleSpinBox* m_mirrorLineX2SpinBox{ nullptr };
    QDoubleSpinBox* m_mirrorLineY2SpinBox{ nullptr };
    QLabel* m_statusLabel{ nullptr };
};
