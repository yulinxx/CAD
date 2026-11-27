#pragma once

#include "ICadViewport.h"
#include "IViewportServices.h"

namespace Apr
{
    // 低耦合 service factory：UI/adapter 不直接构造算法对象；由 customer/AprilCore 提供。
    // 算法（SceneDocument2D/OperationBus/...）在 AprilCore；这里只暴露 ports。
    class ICadServiceFactory
    {
    public:
        virtual ~ICadServiceFactory() = default;

        virtual IViewportFactory* viewportFactory() = 0;        // 2D/3D viewport factory
        virtual IViewportServices* viewportServices() = 0;     // service view（has* / adapter）
    };

    // customer 层可把所有 ports 收敛成一个 factory。
    class CadServices
    {
    public:
        virtual ~CadServices() = default;
        virtual IViewportFactory* viewportFactory() = 0;
        virtual IViewportServices* viewportServices() = 0;
    };
}  // namespace Apr
