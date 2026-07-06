#pragma once

#include <memory>
#include <QString>
#include <QVector>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

class IRenderBackend;

/**
 * @file RenderBackendFactory.h
 * @brief 渲染后端工厂
 *
 * 统一的后端创建入口。UI 层不要直接 new 某个具体渲染器，
 * 而是通过工厂获取抽象接口。
 *
 * 后端类型：
 * - OpenGL：跨平台 OpenGL 4.6 后端
 * - Vulkan：Vulkan 1.3 后端（预留）
 * - Metal：Apple Metal 后端（预留）
 * - Software：纯 CPU 软件渲染后端（预留）
 */
class RENDER_CORE_API RenderBackendFactory
{
public:
    enum class BackendType
    {
        OpenGL,
        Vulkan,
        Metal,
        Software,
    };

    /// 创建指定类型的后端
    static std::unique_ptr<IRenderBackend> create(BackendType type);

    /// 获取可用后端列表
    static QVector<BackendType> availableBackends();

    /// 后端类型名称
    static QString backendTypeName(BackendType type);

    /// 后端类型对应的能力
    static BackendCapability capabilitiesFor(BackendType type);

    /// 默认后端类型（当前平台最佳选择）
    static BackendType defaultBackendType();
};