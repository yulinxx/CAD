#pragma once
// UiEngineAccess 转发头：仅覆盖 Engine2D 算法/填充/离散化子集（include 层部分收口）。
// 场景/实体/选择/编辑 API 仍由 UI 直接 #include "Engine2D/..."；链接层的单一
// 收口点是 CMake 目标 UiEngineAccess。本文件只做转发，不要在这里加代码。
#include "Engine2D/Algorithm/OffsetAlgorithm.h"
