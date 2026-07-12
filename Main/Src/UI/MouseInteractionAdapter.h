/**
 * @file MouseInteractionAdapter.h
 * @brief 鼠标交互式变换适配器 — CAD 拖拽体验
 *
 * 状态机：
 * Idle → Waiting → Dragging → Previewing → Done
 *                        ↗         ↗
 *                   右键取消 → Cancelled
 */
#pragma once

#include "TransformInputProvider.h"
#include <QPointF>

class Viewport2D;

class MouseInteractionAdapter : public ITransformInputProvider
{
public:
    enum class State
    {
        Idle,
        Waiting,
        Dragging,
        Previewing,
        Done,
        Cancelled
    };

    explicit MouseInteractionAdapter(Viewport2D* viewport);
    ~MouseInteractionAdapter() override = default;

    TransformParameters getParameters() override;
    bool hasValidParameters() const override;
    TransformParameters currentParameters() const override;
    void setParametersChangedCallback(std::function<void(const TransformParameters&)> callback) override;
    void setConfirmedCallback(std::function<void(bool confirmed)> callback) override;
    void cancel() override;
    TransformType transformType() const override;

    void setTransformType(TransformType type);
    void startInteraction();
    void endInteraction();
    void confirm();
    State state() const
    {
        return m_state;
    }
    bool isInteracting() const;

    void onMousePressed(const QPointF& worldPos);
    void onMouseMoved(const QPointF& worldPos);
    void onMouseReleased(const QPointF& worldPos);
    void onContextMenu();

private:
    void setState(State newState);
    void updatePreview();
    void resetState();

private:
    Viewport2D* m_viewport{ nullptr };
    TransformType m_transformType{ TransformType::Move };
    TransformParameters m_parameters;
    State m_state{ State::Idle };

    QPointF m_startPos;
    QPointF m_currentPos;
    bool m_leftButtonPressed{ false };

    int m_mirrorClickCount{ 0 };
    QPointF m_mirrorLineStart;
    QPointF m_mirrorLineEnd;

    std::function<void(const TransformParameters&)> m_parametersChangedCallback;
    std::function<void(bool confirmed)> m_confirmedCallback;
};
