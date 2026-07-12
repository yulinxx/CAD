/**
 * @file MouseInteractionAdapter.cpp
 * @brief 鼠标交互式变换适配器实现
 */
#include "MouseInteractionAdapter.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

MouseInteractionAdapter::MouseInteractionAdapter(Viewport2D* viewport)
    : m_viewport(viewport)
{
}

TransformParameters MouseInteractionAdapter::getParameters()
{
    if (m_state == State::Done || m_state == State::Cancelled)
        return m_parameters;

    if (m_state == State::Idle)
        startInteraction();

    return m_parameters;
}

bool MouseInteractionAdapter::hasValidParameters() const
{
    return m_parameters.confirmed;
}

TransformParameters MouseInteractionAdapter::currentParameters() const
{
    return m_parameters;
}

void MouseInteractionAdapter::setParametersChangedCallback(std::function<void(const TransformParameters&)> callback)
{
    m_parametersChangedCallback = callback;
}

void MouseInteractionAdapter::setConfirmedCallback(std::function<void(bool confirmed)> callback)
{
    m_confirmedCallback = callback;
}

void MouseInteractionAdapter::cancel()
{
    if (m_state == State::Idle || m_state == State::Done || m_state == State::Cancelled)
        return;

    m_parameters.confirmed = false;
    setState(State::Cancelled);

    if (m_confirmedCallback)
        m_confirmedCallback(false);

    resetState();
}

void MouseInteractionAdapter::confirm()
{
    if (m_state != State::Previewing)
        return;

    m_parameters.confirmed = true;
    setState(State::Done);

    if (m_confirmedCallback)
        m_confirmedCallback(true);

    resetState();
}

TransformType MouseInteractionAdapter::transformType() const
{
    return m_transformType;
}

void MouseInteractionAdapter::setTransformType(TransformType type)
{
    m_transformType = type;
}

void MouseInteractionAdapter::startInteraction()
{
    resetState();
    setState(State::Waiting);
}

void MouseInteractionAdapter::endInteraction()
{
    resetState();
}

bool MouseInteractionAdapter::isInteracting() const
{
    return m_state == State::Waiting || m_state == State::Dragging || m_state == State::Previewing;
}

void MouseInteractionAdapter::onMousePressed(const QPointF& worldPos)
{
    if (m_state == State::Idle || m_state == State::Done || m_state == State::Cancelled)
        return;

    m_startPos = worldPos;
    m_currentPos = worldPos;
    m_leftButtonPressed = true;

    if (m_transformType == TransformType::Mirror)
    {
        if (m_state == State::Waiting || m_state == State::Previewing)
        {
            m_mirrorLineStart = worldPos;
            m_mirrorClickCount = 1;
            setState(State::Dragging);
        }
    }
    else
    {
        setState(State::Dragging);
    }
}

void MouseInteractionAdapter::onMouseMoved(const QPointF& worldPos)
{
    if (m_state != State::Dragging)
        return;

    m_currentPos = worldPos;

    switch (m_transformType)
    {
        case TransformType::Move:
        {
            double dx = worldPos.x() - m_startPos.x();
            double dy = worldPos.y() - m_startPos.y();

            m_parameters.type = TransformType::Move;
            m_parameters.moveX = dx;
            m_parameters.moveY = dy;
            m_parameters.anchorPoint = m_startPos;
            m_parameters.targetPoint = worldPos;
            m_parameters.isInteractive = true;
            m_parameters.needPreview = true;
            break;
        }
        case TransformType::Rotate:
        {
            double dx = worldPos.x() - m_startPos.x();
            double dy = worldPos.y() - m_startPos.y();
            double angle = std::atan2(dy, dx) * 180.0 / M_PI;

            m_parameters.type = TransformType::Rotate;
            m_parameters.rotateAngle = angle;
            m_parameters.anchorPoint = m_startPos;
            m_parameters.targetPoint = worldPos;
            m_parameters.isInteractive = true;
            m_parameters.needPreview = true;
            break;
        }
        case TransformType::Mirror:
        {
            if (m_mirrorClickCount == 1)
            {
                m_mirrorLineEnd = worldPos;
                m_parameters.type = TransformType::Mirror;
                m_parameters.mirrorAxis = 2;
                m_parameters.mirrorLineX1 = m_mirrorLineStart.x();
                m_parameters.mirrorLineY1 = m_mirrorLineStart.y();
                m_parameters.mirrorLineX2 = worldPos.x();
                m_parameters.mirrorLineY2 = worldPos.y();
                m_parameters.anchorPoint = m_mirrorLineStart;
                m_parameters.targetPoint = worldPos;
                m_parameters.isInteractive = true;
                m_parameters.needPreview = true;
            }
            break;
        }
        default:
            break;
    }

    updatePreview();
}

void MouseInteractionAdapter::onMouseReleased(const QPointF& worldPos)
{
    if (m_state != State::Dragging)
        return;

    m_leftButtonPressed = false;
    m_currentPos = worldPos;

    if (m_transformType == TransformType::Mirror)
    {
        if (m_mirrorClickCount == 1)
        {
            m_mirrorLineEnd = worldPos;
            m_mirrorClickCount = 2;

            m_parameters.type = TransformType::Mirror;
            m_parameters.mirrorAxis = 2;
            m_parameters.mirrorLineX1 = m_mirrorLineStart.x();
            m_parameters.mirrorLineY1 = m_mirrorLineStart.y();
            m_parameters.mirrorLineX2 = m_mirrorLineEnd.x();
            m_parameters.mirrorLineY2 = m_mirrorLineEnd.y();
            m_parameters.anchorPoint = m_mirrorLineStart;
            m_parameters.targetPoint = m_mirrorLineEnd;
            m_parameters.isInteractive = true;

            setState(State::Previewing);
            updatePreview();
        }
    }
    else
    {
        setState(State::Previewing);
        updatePreview();
    }
}

void MouseInteractionAdapter::onContextMenu()
{
    if (m_state == State::Idle || m_state == State::Done || m_state == State::Cancelled)
        return;

    cancel();
}

void MouseInteractionAdapter::setState(State newState)
{
    if (m_state == newState)
        return;
    m_state = newState;
}

void MouseInteractionAdapter::updatePreview()
{
    if (m_parametersChangedCallback)
        m_parametersChangedCallback(m_parameters);
}

void MouseInteractionAdapter::resetState()
{
    m_state = State::Idle;
    m_leftButtonPressed = false;
    m_mirrorClickCount = 0;
    m_mirrorLineStart = QPointF();
    m_mirrorLineEnd = QPointF();
    m_startPos = QPointF();
    m_currentPos = QPointF();
}