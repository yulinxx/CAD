#pragma once

#include <QString>
#include <QStringList>
#include <QSize>
#include <QImage>

#include <memory>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

class EntityDocument2D;
class SceneDocument3D;
class CameraController3D;
struct RenderContext;
struct RenderFrame;

namespace Eg { class SceneManager; }

/**
 * @file IRenderBackend.h
 * @brief 统一渲染后端抽象接口
 *
 * 这是后续 OpenGL / Vulkan / Metal / 软件渲染都要实现的共同接口。
 * UI 层只依赖这个抽象，不直接依赖具体图形 API。
 *
 * 职责边界：
 * - 初始化 / 销毁 GPU 资源
 * - 绑定场景和相机
 * - 编译场景为渲染数据
 * - 执行一帧渲染
 * - 输出渲染结果（帧缓冲、调试快照、统计信息）
 *
 * 不应承担：
 * - 场景遍历逻辑（由 SceneCompiler 负责）
 * - 业务状态编排（由上层负责）
 * - UI 控件管理（由 Viewport 负责）
 */
class RENDER_CORE_API IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    // ============ 生命周期 ============

    /// 初始化后端（创建 GPU 资源、上下文等）
    /// @param nativeWindowHandle 原生窗口句柄，软件后端可为 nullptr
    virtual bool initialize(void* nativeWindowHandle = nullptr) = 0;

    /// 释放后端资源
    virtual void shutdown() = 0;

    /// 后端是否已就绪
    virtual bool isReady() const = 0;

    // ============ 上下文绑定 ============

    /// 绑定渲染上下文（视口大小、模式等）
    virtual void bindContext(const RenderContext& context) = 0;

    /// 获取当前渲染上下文
    virtual const RenderContext& context() const = 0;

    // ============ 场景绑定 ============

    /// 绑定 2D 场景文档（旧版 EntityDocument2D 路径）
    virtual void setScene(EntityDocument2D* document) = 0;

    /// 绑定 2D 场景（新版 Eg::SceneManager 路径）
    virtual void setScene(Eg::SceneManager* scene) = 0;

    /// 绑定 3D 场景文档
    virtual void setScene(SceneDocument3D* document) = 0;

    /// 绑定相机控制器
    virtual void setCamera(CameraController3D* controller) = 0;

    // ============ 渲染管线 ============

    /// 编译场景为渲染帧数据（遍历场景、生成批次、裁剪、缓存）
    virtual void compile() = 0;

    /// 提交编译好的渲染帧数据（由 SceneCompiler 产出，后端负责渲染）
    virtual void submitFrame(const RenderFrame& frame) = 0;

    /// 渲染一帧
    virtual void render() = 0;

    /// 开始一帧（用于多 pass 渲染）
    virtual void beginFrame() = 0;

    /// 结束一帧（交换缓冲区）
    virtual void endFrame() = 0;

    // ============ 视口控制 ============

    /// 调整视口大小
    virtual void resize(const QSize& size) = 0;

    /// 重置视图
    virtual void resetView() = 0;

    // ============ 模式切换 ============

    /// 切换轨道模式
    virtual void setOrbitMode(bool enabled) = 0;

    /// 切换测量模式
    virtual void setMeasureMode(bool enabled) = 0;

    /// 设置渲染模式
    virtual void setRenderMode(RenderMode mode) = 0;

    /// 获取当前渲染模式
    virtual RenderMode renderMode() const = 0;

    // ============ 帧输出 ============

    /// 捕获当前帧为调试快照（软件后端直接使用，GPU 后端通过 glReadPixels）
    virtual QImage captureFrame() const = 0;

    /// 获取上一帧的渲染统计
    virtual RenderStatistics getStatistics() const = 0;

    // ============ 后端信息 ============

    /// 当前后端类型名称
    virtual QString backendName() const = 0;

    /// 查询后端是否支持指定能力
    virtual bool supportsCapability(BackendCapability cap) const = 0;

    /// 获取后端支持的所有能力
    virtual BackendCapability capabilities() const = 0;
};