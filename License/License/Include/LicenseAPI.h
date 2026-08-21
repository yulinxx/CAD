#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #ifdef LICENSE_EXPORTS
        #define LICENSE_API __declspec(dllexport)
    #else
        #define LICENSE_API __declspec(dllimport)
    #endif
    #ifdef LICENSE_KEYGEN_EXPORTS
        #define LICENSE_KEYGEN_API __declspec(dllexport)
    #else
        #define LICENSE_KEYGEN_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #ifdef LICENSE_EXPORTS
        #define LICENSE_API __attribute__((visibility("default")))
    #else
        #define LICENSE_API
    #endif
    #ifdef LICENSE_KEYGEN_EXPORTS
        #define LICENSE_KEYGEN_API __attribute__((visibility("default")))
    #else
        #define LICENSE_KEYGEN_API
    #endif
#else
    #define LICENSE_API
    #define LICENSE_KEYGEN_API
#endif

#ifdef __cplusplus
    #define LICENSE_C_API extern "C"
#else
    #define LICENSE_C_API extern
#endif
