#pragma once

#include <memory>
#include <string>
#include <vector>

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
 * 职责单一：只做创建，不做策略膨胀。
 * - 配置解析委托给 BackendConfigResolver
 * - 能力查询委托给 BackendCapabilityRegistry
 * - 字符串转换委托给 BackendCapabilityRegistry
 *
 * 后端类型：
 * - OpenGL：跨平台 OpenGL 后端
 * - Vulkan：Vulkan 后端（预留）
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

    static std::unique_ptr<IRenderBackend> create(BackendType type);

    static std::vector<BackendType> availableBackends();

    static std::string backendTypeName(BackendType type);

    static BackendCapability capabilitiesFor(BackendType type);

    static BackendType defaultBackendType();

    static BackendType fromString(const std::string& name);

    static std::string toString(BackendType type);

    static BackendType backendFromEnvironment();

    static std::unique_ptr<IRenderBackend> createConfigured();

private:
    static int toRegistryType(BackendType type);
    static BackendType fromRegistryType(int type);
};
