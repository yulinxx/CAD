#pragma once
// UiEngineAccess 转发头：统一失败契约（错误码 / 错误上下文 / 文案映射）。
// UI 侧必须经由本文件取错误类型与文案，禁止直接 #include "Engine/Diagnostics/..."。
// 本文件只做转发，不要在这里加代码。
#include "Engine/Diagnostics/ErrorContract.h"
