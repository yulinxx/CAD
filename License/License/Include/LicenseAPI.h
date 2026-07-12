#ifndef LICENSE_API_H
#define LICENSE_API_H

#if defined(_WIN32) || defined(_WIN64)
#ifdef LICENSE_EXPORTS
#define LICENSE_API __declspec(dllexport)
#else
#define LICENSE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifdef LICENSE_EXPORTS
#define LICENSE_API __attribute__((visibility("default")))
#else
#define LICENSE_API
#endif
#else
#define LICENSE_API
#endif

#ifdef __cplusplus
#define LICENSE_C_API extern "C"
#else
#define LICENSE_C_API extern
#endif

#endif
