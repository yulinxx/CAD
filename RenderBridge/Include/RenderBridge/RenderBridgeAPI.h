/**
 * @file RenderBridgeAPI.h
 * @brief RenderBridge 模块导出宏定义
 */
#pragma once

// Windows
#if defined(_WIN32) || defined(_WIN64)
    #ifdef RENDERBRIDGE_EXPORTS
        #define RENDERBRIDGE_API __declspec(dllexport)
    #else
        #define RENDERBRIDGE_API __declspec(dllimport)
    #endif
// Linux/Unix
#elif defined(__GNUC__) || defined(__clang__)
    #ifdef RENDERBRIDGE_EXPORTS
        #define RENDERBRIDGE_API __attribute__((visibility("default")))
    #else
        #define RENDERBRIDGE_API
    #endif
#else
    #define RENDERBRIDGE_API
#endif
