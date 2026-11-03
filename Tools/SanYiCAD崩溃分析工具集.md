# SanYiCAD 崩溃分析工具集

## 目录

- [概述](#概述)
- [工具清单](#工具清单)
- [环境依赖](#环境依赖)
- [crash_analyze.py - 本地分析器](#crash_analyzepy---本地分析器)
  - [基本用法](#基本用法)
  - [参数说明](#参数说明)
  - [输出内容](#输出内容)
  - [退出码](#退出码)
- [crash_server.py - 远程分析服务器](#crash_serverpy---远程分析服务器)
  - [启动服务](#启动服务)
  - [API 接口](#api-接口)
- [crash_reporter.py - 上报客户端](#crash_reporterpy---上报客户端)
- [crash_handler.h - C++ 崩溃回调](#crash_handlerh---c-崩溃回调)
- [符号化工具配置](#符号化工具配置)
- [Minidump 格式原理](#minidump-格式原理)
- [分析流程详解](#分析流程详解)
- [常见异常码速查](#常见异常码速查)
- [部署方案](#部署方案)
  - [整体架构](#整体架构)
  - [方案 1：自建完整方案](#方案-1自建完整方案)
  - [方案 2：接入 Sentry](#方案-2接入-sentry)
  - [方案 3：局域网快速部署](#方案-3局域网快速部署)
  - [方案 4：CI/CD 自动创建 Issue](#方案-4cicd-自动创建-issue)
- [环境部署指南](#环境部署指南)
  - [Windows 开发环境](#windows-开发环境)
  - [Linux 服务器部署](#linux-服务器部署)
  - [Docker 部署](#docker-部署)
  - [Windows Server 部署](#windows-server-部署)
  - [HTTPS 配置](#https-配置)
  - [进程管理](#进程管理)
  - [防火墙配置](#防火墙配置)
  - [监控与告警](#监控与告警)
- [FAQ](#faq)

---

## 概述

本工具集用于分析 SanYiCAD 程序的崩溃转储文件（`.dmp`），支持以下工作流：

1. **本地分析**：开发者在本地直接分析 dmp 文件
2. **远程分析**：客户端崩溃后自动上传到服务器，服务器返回分析结果
3. **C++ 集成**：在 CAD 程序中嵌入崩溃回调，崩溃时自动生成 dmp 并上报

支持的 dump 格式：Breakpad / Crashpad 在 Windows / Linux / macOS 上生成的 minidump 文件。

---

## 工具清单

| 文件 | 类型 | 功能 |
|------|------|------|
| `crash_analyze.py` | Python | 核心分析引擎，解析 minidump 结构并输出报告 |
| `crash_server.py` | Python | HTTP 服务器，接收 dmp 文件并调用分析引擎 |
| `crash_reporter.py` | Python | 上报客户端，将 dmp 文件发送到服务器 |
| `crash_handler.h` | C++ Header | Windows 崩溃回调，自动生成 dmp 并调用上报 |

---

## 环境依赖

### 必需

- Python 3.8+（仅使用标准库，无需 pip install）

### 可选（用于符号化）

| 工具 | 来源 | 作用 |
|------|------|------|
| `llvm-symbolizer` | LLVM 安装包 | 将地址解析为函数名+行号 |
| `addr2line` | GNU binutils / MinGW | 同上 |
| `atos` | macOS Xcode 自带 | 同上 |
| `minidump_stackwalk` | Google Breakpad 源码构建 | 完整的调用栈还原 |

安装 LLVM（Windows）：
```powershell
winget install LLVM.LLVM
```

---

## crash_analyze.py - 本地分析器

### 基本用法

```bash
# 最简用法：直接分析 dmp 文件
python crash_analyze.py crash.dmp

# 指定构建目录（用于查找 PDB/debug 符号）
python crash_analyze.py crash.dmp --search-dir build/Debug --search-dir build/bin

# 使用 minidump_stackwalk 做完整符号化
python crash_analyze.py crash.dmp --dump-stackwalk minidump_stackwalk.exe

# 仅解析结构，跳过符号化（速度最快）
python crash_analyze.py crash.dmp --raw

# 输出到文件（禁用颜色）
python crash_analyze.py crash.dmp --no-color > report.txt
```

### 参数说明

| 参数 | 说明 |
|------|------|
| `dump` | `.dmp` 文件路径（必填） |
| `--search-dir DIR` | 构建输出目录，用于查找调试二进制文件（可多次指定） |
| `--symbols-dir DIR` | Breakpad `.sym` 符号文件目录 |
| `--dump-stackwalk PATH` | 外部 `minidump_stackwalk` 二进制路径 |
| `--raw` | 仅解析 dump 结构，跳过符号化 |
| `--no-color` | 禁用彩色输出 |

### 输出内容

脚本按以下顺序输出分析报告：

```
1. 崩溃摘要          - 异常原因、故障地址、崩溃 PC、系统平台、时间
2. Minidump 头信息    - 版本号、Stream 数量、各 Stream 偏移
3. 系统信息          - 操作系统、CPU 架构、核心数
4. 加载模块列表      - 基址、大小、时间戳、名称/路径
5. 异常/崩溃原因     - 异常码、原因描述、故障地址、异常参数
6. 崩溃线程寄存器    - PC/SP/FP/LR + 所有通用寄存器
7. 堆栈内存 Hex Dump - SP 附近内存的十六进制+ASCII 视图
8. 符号化回溯        - 调用栈（优先 minidump_stackwalk，否则内置 walker）
9. 其他线程概览      - 所有线程的 PC 及所在模块
```

### 退出码

| 码 | 含义 |
|----|------|
| 0 | 解析成功 |
| 1 | 文件无法解析（不是有效的 minidump） |
| 2 | 解析成功，但崩溃 PC 无法映射到任何已加载模块 |

---

## crash_server.py - 远程分析服务器

### 启动服务

```bash
# 默认监听 0.0.0.0:8080
python crash_server.py

# 指定端口和符号化工具
python crash_server.py --port 9000 --stackwalk /path/to/minidump_stackwalk

# 指定搜索目录
python crash_server.py --search-dir build/Debug --search-dir build/bin
```

### API 接口

#### `POST /analyze`

上传 `.dmp` 文件进行分析。

**请求：**
- Method: `POST`
- Content-Type: `application/octet-stream`
- Body: dmp 文件的原始二进制内容

**响应（JSON）：**
```json
{
  "system_info": {
    "os": "Win32NT",
    "os_version": "10.0.26200",
    "arch": "amd64",
    "cpus": 16
  },
  "crash_summary": {
    "exception_code": "0xc0000409",
    "exception_reason": "0xc0000409",
    "crash_address": "0x00007ffa0af0527e",
    "crash_module": "ucrtbase.dll",
    "crash_offset": "0xa527e",
    "thread_id": 13840
  },
  "exception": {
    "code": "0xc0000409",
    "reason": "0xc0000409",
    "address": "0x00007ffa0af0527e",
    "thread_id": 13840,
    "parameters": ["0x0000000000000007"]
  },
  "registers": {
    "pc": "0x00007ffa0af0527e",
    "sp": "0x000000a3d7cf64f0",
    "fp": "0x000000a3d7cf6650",
    "lr": "0x00007ffa0af0527e"
  },
  "modules": [...],
  "backtrace": [
    {"address": "0x00007ffa0af0527e", "module": "ucrtbase.dll", "offset": "0xa527e"},
    {"address": "0x00007ffa0ae61234", "module": "CAD.exe", "offset": "0x51234"}
  ],
  "analysis_time_ms": 45.2,
  "file_size_bytes": 10073931
}
```

#### `GET /health`

健康检查。

```json
{"status": "ok", "service": "Crash Analyzer"}
```

#### `GET /api/help`

查看 API 文档和客户端示例。

---

## crash_reporter.py - 上报客户端

### 独立使用

```bash
python crash_reporter.py http://server:8080 crash.dmp
```

### 作为库使用

```python
from crash_reporter import CrashReporter

reporter = CrashReporter("http://server:8080")
result = reporter.report("crash.dmp")
print(result["crash_summary"])
```

### 返回值

返回服务器返回的 JSON 字典，包含 `crash_summary`、`backtrace`、`modules` 等字段。连接失败时返回 `{"error": "..."}`.

---

## crash_handler.h - C++ 崩溃回调

### 集成方式

```cpp
#include "crash_handler.h"

int main() {
    // 初始化崩溃处理器，传入服务器地址
    INIT_CRASH_HANDLER(L"http://your-server:8080");
    
    // ... 你的 CAD 代码
    return 0;
}
```

### 编译要求

```cmake
# CMakeLists.txt
target_link_libraries(your_target PRIVATE dbghelp)
```

### 工作流程

程序崩溃时自动执行：

```
1. 触发未处理异常过滤器 (SetUnhandledExceptionFilter)
2. 调用 MiniDumpWriteDump 生成 .dmp 文件到 %TEMP%
3. 调用 crash_reporter.py 上传到服务器
4. 弹出 MessageBox 显示崩溃信息
```

### 崩溃对话框内容

```
程序崩溃了！

异常代码: 0xC0000005
崩溃地址: 0x00007FFB12345678
崩溃报告已保存:
C:\Users\xx\AppData\Local\Temp\Crash_20260814_052500.dmp

请将此文件发送给开发人员。
```

---

## 符号化工具配置

### 什么是符号化

符号化是将内存地址转换为「函数名 + 源文件 + 行号」的过程。

```
未符号化:  0x00007ffa0af0527e
已符号化:  __security_check_cookie+0x1e (ucrtbase.dll)
           security.c:523
```

### 工具优先级

脚本按以下顺序自动检测可用工具：

1. `atos`（macOS 自带）
2. `addr2line`（GNU binutils / MinGW）
3. `llvm-symbolizer`（LLVM）
4. `minidump_stackwalk`（Breakpad，最完整）

### Windows 推荐配置

```powershell
# 方式 1：安装 LLVM（推荐）
winget install LLVM.LLVM
# 安装后 llvm-symbolizer 自动在 PATH 中

# 方式 2：使用 Visual Studio 自带的 llvm-addr2line
# 路径: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\llvm-addr2line.exe
```

### PDB → .sym 符号转换（关键步骤）

`minidump_stackwalk` **不认识原生 PDB**，只认 Breakpad 的文本符号格式（`.sym`）。必须在**构建阶段**用 `dump_syms` 完成转换。

#### dump_syms 工具获取

```bash
# 方式 1：从 Breakpad 源码构建（推荐）
git clone https://chromium.googlesource.com/breakpad/breakpad
cd breakpad
mkdir -p out && cd out
cmake .. -DBUILD_SHARED_LIBS=OFF
cmake --build . --target dump_syms
# 产物: out/Release/dump_syms.exe

# 方式 2：从 GitHub Release 下载预编译版
# https://github.com/nicedoc/nicedoc/releases （搜索 dump_syms）
```

#### 转换命令

```powershell
# 对每个编译产物执行 dump_syms
# dump_syms.exe <PDB路径> > <输出.sym>

.\dump_syms.exe build\bin\RelWithDebInfo\SanYiCAD.pdb > SanYiCAD.exe.sym
.\dump_syms.exe build\bin\RelWithDebInfo\UI2D.pdb > UI2D.dll.sym
.\dump_syms.exe build\bin\RelWithDebInfo\Qt6Core.pdb > Qt6Core.dll.sym
```

#### .sym 文件格式

```
MODULE windows x86_64 12345678ABCDEF0123456789ABCDEF01 SanYiCAD.exe
INFO CODE_ID 1234567890 ABCDEF0123456789ABCDEF01
FILE 0 src/main.cpp
FILE 1 src/app.cpp
FUNC 1000 50 0 main
1000 10 0 src/main.cpp 15
1010 20 0 src/main.cpp 16
FUNC 1100 a0 0 App::init
1100 10 0 src/app.cpp 42
```

#### 符号目录结构（必须严格遵守）

```
symbols/
  SanYiCAD.exe/
    12345678ABCDEF01/          ← dump_syms 输出第一行的 GUID
      SanYiCAD.exe.sym
  UI2D.dll/
    ABCDEF0123456789/
      UI2D.dll.sym
  Qt6Core.dll/
    1122334455667788/
      Qt6Core.dll.sym
  ntdll.dll/                  ← 系统库通常不需要符号
    ...
```

> **为什么需要 GUID？** `minidump_stackwalk` 会根据 dmp 里记录的模块 CodeView 签名自动匹配对应 GUID 目录下的 `.sym` 文件。

### 使用 minidump_stackwalk（最完整）

```bash
# 构建 minidump_stackwalk（Linux 服务器上）
git clone https://chromium.googlesource.com/breakpad/breakpad
cd breakpad && mkdir -p out && cd out
cmake .. -DBUILD_SHARED_LIBS=OFF
cmake --build . --target minidump_stackwalk
# 产物: out/Release/minidump_stackwalk

# 使用
python crash_analyze.py crash.dmp --dump-stackwalk out/Release/minidump_stackwalk
```

### PDB 符号文件（本地开发用）

对于 Windows 本地开发，确保在构建时生成 PDB 文件：

```cmake
# CMakeLists.txt
set(CMAKE_BUILD_TYPE RelWithDebInfo)  # 或 Debug
# PDB 文件会自动生成在输出目录
```

分析时指定构建目录：
```bash
python crash_analyze.py crash.dmp --search-dir build/bin/RelWithDebInfo
```

---

## Minidump 格式原理

### 文件结构

```
┌──────────────────────────────┐
│  MINIDUMP_HEADER (32 bytes)  │  签名 "MDMP"、版本、Stream 数量
├──────────────────────────────┤
│  Stream Directory            │  每个 Stream 的类型、大小、RVA
│  [Stream 0]                  │
│  [Stream 1]                  │
│  ...                         │
├──────────────────────────────┤
│  Stream 0 数据               │  实际内容（如 SystemInfo）
│  Stream 1 数据               │
│  ...                         │
└──────────────────────────────┘
```

### 关键 Stream 类型

| 类型 | 名称 | 内容 |
|------|------|------|
| 3 | ThreadList | 所有线程的 ID、栈基址、寄存器上下文位置 |
| 4 | ModuleList | 所有已加载模块的基址、大小、名称 |
| 5 | MemoryList | 捕获的内存区域（通常包含线程栈） |
| 6 | Exception | 异常代码、故障地址、崩溃线程、寄存器上下文 |
| 7 | SystemInfo | 操作系统、CPU 架构、处理器数量 |

### 模块记录结构 (MDRawModule)

```
偏移  大小  字段
+0    8    base_of_image      模块加载基址
+8    4    size_of_image      模块映像大小
+12   4    checksum           校验和
+16   4    time_date_stamp    PE 时间戳
+20   4    module_name_rva    -> MINIDUMP_STRING (完整路径)
+24   8    cv_record          -> CodeView 记录 (PDB/GUID 信息)
```

脚本使用自适应 stride 探测来处理不同版本 Breakpad/Crashpad 生成的不同大小的模块记录（56/100/108/112/120 字节等）。

### 寄存器上下文

崩溃时 CPU 的完整状态，包含：

| 寄存器 | AMD64 | ARM64 | 用途 |
|--------|-------|-------|------|
| PC | RIP | PC | 当前执行指令地址 |
| SP | RSP | SP | 栈顶指针 |
| FP | RBP | X29 | 帧指针（用于回溯调用栈） |
| LR | - | X30 | 链接寄存器（返回地址） |

### 帧指针回溯原理

```
栈帧布局（64位）:
┌──────────────┐ 高地址
│ 局部变量      │
├──────────────┤
│ 返回地址      │ FP+8
├──────────────┤
│ 旧的 FP      │ FP -> [next_fp]
└──────────────┘ 低地址

回溯过程:
1. 读取当前 FP 指向的值 → 得到上一层 FP
2. 读取 FP+8 的值 → 得到返回地址
3. 返回地址 - 模块基址 → 模块内偏移
4. 重复直到 FP=0 或越界
```

---

## 分析流程详解

```
                    ┌─────────────────┐
                    │  读取 .dmp 文件  │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │  解析文件头      │  签名验证、Stream 目录
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
     ┌────────▼───────┐ ┌───▼───────┐ ┌───▼────────┐
     │  SystemInfo     │ │ ModuleList│ │ Exception  │
     │  OS/CPU 架构    │ │ 已加载模块│ │ 异常信息    │
     └────────┬───────┘ └───┬───────┘ └───┬────────┘
              │              │              │
              │     ┌────────▼────────┐     │
              │     │  自适应 stride   │     │
              │     │  探测模块记录    │     │
              │     └────────┬────────┘     │
              │              │              │
              │     ┌────────▼────────┐     │
              │     │  匹配 PC 到模块  │◄────┘
              │     └────────┬────────┘
              │              │
              │     ┌────────▼────────┐
              │     │  解析寄存器上下文 │
              │     └────────┬────────┘
              │              │
              │     ┌────────▼────────┐
              │     │  帧指针回溯      │
              │     │  (内置 walker)   │
              │     └────────┬────────┘
              │              │
              │     ┌────────▼────────┐
              │     │  符号化地址      │  addr2line / llvm-symbolizer
              │     │  → 函数名+行号   │
              │     └────────┬────────┘
              │              │
     ┌────────▼──────────────▼────────┐
     │         输出分析报告            │
     └────────────────────────────────┘
```

---

## 常见异常码速查

### Windows SEH 异常

| 异常码 | 名称 | 常见原因 |
|--------|------|---------|
| `0xC0000005` | ACCESS_VIOLATION | 空指针解引用、野指针、use-after-free |
| `0xC00000FD` | STACK_OVERFLOW | 无限递归、栈上大数组 |
| `0xC0000094` | INTEGER_DIVIDE_BY_ZERO | 除以零 |
| `0xC000001D` | ILLEGAL_INSTRUCTION | 代码损坏、CPU 不支持的指令 |
| `0xC000008C` | ARRAY_BOUNDS_EXCEEDED | 数组越界（/GS 检测） |
| `0xC0000409` | STATUS_STACK_BUFFER_OVERRUN | 栈缓冲区溢出（/GS 检测） |
| `0x80000003` | BREAKPOINT | 调试器断点、`__debugbreak()` |

### Linux 信号

| 信号 | 名称 | 常见原因 |
|------|------|---------|
| `SIGSEGV` (11) | 段错误 | 空指针、野指针、栈溢出 |
| `SIGABRT` (6) | 异常终止 | `abort()` 调用、断言失败 |
| `SIGFPE` (8) | 浮点异常 | 除以零 |
| `SIGILL` (4) | 非法指令 | 代码损坏 |
| `SIGBUS` (7) | 总线错误 | 内存未对齐 |

### macOS Mach 异常

| 异常码 | 名称 |
|--------|------|
| 1 | EXC_BAD_ACCESS (无效地址) |
| 6 | EXC_BREAKPOINT |
| 11 | EXC_CRASH |

---

## 部署方案

### 整体架构

```
┌─────────────────┐     编译时        ┌──────────────────┐
│    CI/CD        │ ────────────────> │   Symbol Store   │
│  (PDB 生成)     │  dump_syms 转换   │  (.sym 文件)     │
└─────────────────┘                   └──────────────────┘
         │                                   ▲
         │ 版本→GUID映射                     │ 按需下载符号
         ▼                                   │
┌─────────────────┐    崩溃时上传      ┌──────────────────┐
│   客户端程序     │ ────────────────> │   分析服务器      │
│  (Crashpad)     │     .dmp 文件     │  (Linux 容器)    │
└─────────────────┘                   └──────────────────┘
                                              │
                                              ▼
                                       ┌──────────────┐
                                       │ minidump_    │
                                       │ stackwalk    │
                                       └──────────────┘
```

---

### 方案 1：自建完整方案（推荐）

#### 1.1 CI/CD 构建时生成符号

```powershell
# Windows CI 脚本（构建后执行）

$modules = @(
    "SanYiCAD.exe",
    "UI2D.dll", "UI3D.dll", "UICommon.dll",
    "Engine2D.dll", "Engine3D.dll", "EngineCommon.dll",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"
)

$symbolRoot = "symbols"
New-Item -ItemType Directory -Force -Path $symbolRoot

foreach ($mod in $modules) {
    $pdb = "$buildDir\$mod.pdb"
    if (!(Test-Path $pdb)) { continue }

    # 1. 转换 PDB → .sym
    .\dump_syms.exe $pdb > "$mod.sym"

    # 2. 提取 GUID（.sym 第一行第4列）
    $line = Get-Content "$mod.sym" -Head 1
    $parts = $line -split '\s+'
    $guid = $parts[3]
    $name = $parts[4]

    # 3. 按 Breakpad 标准目录结构存放
    $dest = "$symbolRoot\$name\$guid"
    New-Item -ItemType Directory -Force -Path $dest
    Move-Item "$mod.sym" "$dest\$name.sym" -Force
}

# 4. 上传到符号存储
aws s3 sync $symbolRoot s3://your-crash-symbols/symbols/ --delete
# 或 MinIO: mc mirror $symbolRoot myminio/crash-symbols/symbols/
# 或 NAS:  xcopy /E /Y $symbolRoot \\nas\crash-symbols\symbols\
```

**版本关联记录（存入数据库）：**

```sql
CREATE TABLE symbol_versions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    version     TEXT NOT NULL,      -- 'v2.1.0'
    build_id    TEXT NOT NULL,      -- '20260814.3'
    git_commit  TEXT,               -- 'abc1234'
    module_name TEXT NOT NULL,      -- 'SanYiCAD.exe'
    guid        TEXT NOT NULL,      -- '12345678ABCDEF'
    sym_path    TEXT NOT NULL,      -- 's3://.../SanYiCAD.exe/12345678ABCDEF/'
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

#### 1.2 客户端集成（Crashpad）

推荐用 Crashpad（Google 维护，支持 Windows/Linux/macOS）：

```cpp
#include <client/crashpad_client.h>
#include <client/crashpad_info.h>

bool InitCrashpad(const std::string& upload_url) {
    // 基础目录
    base::FilePath database(L"C:\\ProgramData\\SanYiCAD\\crashpad");
    base::FilePath metrics(L"C:\\ProgramData\\SanYiCAD\\crashpad\\metrics");

    // 启动 Crashpad Handler
    crashpad::CrashpadClient client;
    std::string handler;
    crashpad::CrashpadClient::GetHandler(&handler);

    // 附加信息（会出现在崩溃报告中）
    std::map<std::string, std::string> annotations;
    annotations["product"] = "SanYiCAD";
    annotations["version"] = SANVICAD_VERSION;
    annotations["build_id"] = SANVICAD_BUILD_ID;
    annotations["platform"] = "windows";

    // 启动
    bool success = client.StartHandler(
        base::FilePath(handler),
        database,
        metrics,
        upload_url,        // 你的服务器上传接口
        annotations,
        {},                 // arguments
        true,               // restartable
        true                // asynchronous_start
    );

    // 设置崩溃报告级别
    crashpad::CrashpadInfo* info = crashpad::CrashpadInfo::GetCrashpadInfo();
    info->set_system_board_name("SanYiCAD");

    return success;
}
```

**CMake 集成：**
```cmake
# 第三方依赖
add_subdirectory(third_party/crashpad EXCLUDE_FROM_ALL)

target_link_libraries(SanYiCAD PRIVATE
    crashpad_client
    crashpad_handler
    crashpad_util
)
```

#### 1.3 服务器端分析

```python
#!/usr/bin/env python3
"""
服务器端崩溃自动分析服务
接收 dmp → 下载符号 → minidump_stackwalk → 返回结构化结果
"""
import os
import subprocess
import tempfile
import json
import hashlib
from pathlib import Path
from typing import Optional

# --- 配置 ---
SYMBOL_ROOT = "/opt/crash-symbols"          # 本地符号缓存目录
SYMBOL_REMOTE = "s3://your-crash-symbols"   # 远程符号存储
STACKWALK_BIN = "/usr/local/bin/minidump_stackwalk"

# --- 符号下载 ---
def download_symbols(modules_needed: list) -> dict:
    """
    从远程存储按需下载 .sym 文件
    modules_needed: [(name, guid), ...]
    返回: {name: local_sym_path}
    """
    import boto3
    s3 = boto3.client('s3')

    sym_paths = {}
    for name, guid in modules_needed:
        local_dir = Path(SYMBOL_ROOT) / name / guid
        local_file = local_dir / f"{name}.sym"

        if local_file.exists():
            sym_paths[name] = str(local_file)
            continue

        local_dir.mkdir(parents=True, exist_ok=True)
        s3_key = f"symbols/{name}/{guid}/{name}.sym"
        try:
            s3.download_file("your-crash-symbols", s3_key, str(local_file))
            sym_paths[name] = str(local_file)
        except Exception:
            pass  # 系统库没有符号，跳过

    return sym_paths

# --- 分析 ---
def analyze_dump(dmp_path: str) -> dict:
    """调用 minidump_stackwalk 做完整符号化分析"""

    cmd = [
        STACKWALK_BIN,
        dmp_path,
        SYMBOL_ROOT,
        "-d",    # 详细输出
        "-r",    # 输出 JSON
    ]

    result = subprocess.run(
        cmd, capture_output=True, text=True, timeout=120
    )

    # minidump_stackwalk 输出 JSON
    try:
        analysis = json.loads(result.stdout)
    except json.JSONDecodeError:
        # 回退到文本解析
        analysis = parse_text_output(result.stdout)

    return analysis

def parse_text_output(output: str) -> dict:
    """解析 minidump_stackwalk 的文本输出"""
    lines = output.splitlines()
    result = {
        "crash_reason": "",
        "crash_address": "",
        "threads": [],
    }

    for i, line in enumerate(lines):
        if line.startswith("Crash reason:"):
            result["crash_reason"] = line.split(":", 1)[1].strip()
        elif line.startswith("Crash address:"):
            result["crash_address"] = line.split(":", 1)[1].strip()
        elif line.startswith("Thread"):
            thread = parse_thread_block(lines, i)
            if thread:
                result["threads"].append(thread)

    return result

def parse_thread_block(lines, start_idx) -> Optional[dict]:
    """解析单个线程的堆栈块"""
    thread_line = lines[start_idx]
    crashed = "(crashed)" in thread_line

    frames = []
    idx = start_idx + 1
    while idx < len(lines):
        line = lines[idx]
        if not line.startswith(" ") and line.strip():
            break
        parts = line.strip().split()
        if len(parts) >= 4:
            frames.append({
                "frame": int(parts[0]),
                "module": parts[1],
                "function": parts[2] if len(parts) > 3 else "",
                "offset": parts[-1] if len(parts) > 3 else parts[2],
            })
        idx += 1

    return {
        "crashed": crashed,
        "frame_count": len(frames),
        "frames": frames,
    }

# --- 崩溃指纹（去重聚合） ---
def fingerprint(frames: list, top_n=5) -> str:
    """用顶部N帧的 模块+偏移 生成崩溃签名"""
    sig = "|".join([
        f"{f['module']}:{f.get('offset', f.get('function', '0'))}"
        for f in frames[:top_n]
    ])
    return hashlib.sha256(sig.encode()).hexdigest()[:16]
```

#### 1.4 minidump_stackwalk 输出示例

```
Crash reason:  EXCEPTION_STACK_BUFFER_OVERRUN
Crash address: 0x7ffa0af0527e
Process uptime: 25 seconds

Thread 0 (crashed)
 0  ucrtbase.dll!abort + 0x12e
 1  ucrtbase.dll!terminate + 0x22
 2  VCRUNTIME140_1.dll!__FrameUnwindToState + 0x310
 3  VCRUNTIME140_1.dll!__CxxFrameHandler4 + 0x2e0
 4  UICommon.dll!SettingsDialogBase::ensureTabLoaded(int) + 0x45 (SettingsDialogBase.cpp:109)
 5  UICommon.dll!SettingsDialogBase::onCurrentTabChanged(int) + 0x18 (SettingsDialogBase.cpp:153)
 6  Qt6Widgets.dll!QTabWidget::qt_static_metacall + 0x123
 ...
```

---

### 方案 2：接入 Sentry（最快路径）

如果不想自建整套系统，**强烈推荐直接用 [Sentry](https://sentry.io)**：

- 支持原生 Windows minidump 上传
- 支持 PDB / Breakpad symbol 上传
- 自动符号化、去重、告警、统计
- 客户端 SDK 成熟（sentry-native）

#### 上传符号到 Sentry

```bash
# 安装 sentry-cli
npm install -g @sentry/cli

# 登录
sentry-cli login

# 上传符号文件
sentry-cli upload-dif \
    --org your-org \
    --project your-project \
    --wait \
    symbols/

# 或者直接上传 PDB（Sentry 会自动转换）
sentry-cli upload-dif \
    --org your-org \
    --project your-project \
    --type pdb \
    build/bin/RelWithDebInfo/
```

#### 客户端集成

```cpp
#include <sentry.h>

void InitSentry() {
    sentry_options_t *options = sentry_options_new();
    sentry_options_set_dsn(options,
        "https://xxx@xxx.ingest.sentry.io/xxx");
    sentry_options_set_release(options, "sanyicad@" SANVICAD_VERSION);
    sentry_options_set_database_path(options,
        "C:\\ProgramData\\SanYiCAD\\sentry");
    sentry_init(options);

    // 设置附加信息
    sentry_set_tag("build_id", SANVICAD_BUILD_ID);
    sentry_set_tag("platform", "windows");
}

// 程序退出时
void ShutdownSentry() {
    sentry_close();
}
```

#### Sentry vs 自建对比

| 特性 | Sentry | 自建 |
|------|--------|------|
| 部署难度 | 云服务，零运维 | 需要维护服务器+符号存储 |
| 符号上传 | `sentry-cli` 一键上传 | 需自己实现 dump_syms + S3 |
| 去重聚合 | 自动 | 需自己实现指纹算法 |
| 告警通知 | 内置邮件/Slack/钉钉 | 需自己对接 |
| 数据所有权 | 数据在 Sentry 服务器 | 数据在你自己的服务器 |
| 费用 | 免费层 5k 事件/月 | 服务器成本 |
| 定制化 | 有限 | 完全可控 |

---

### 方案 3：局域网快速部署

适合开发团队内部使用：

```
开发者 PC                          开发服务器
┌──────────┐    HTTP POST         ┌──────────────────┐
│ CAD 崩溃  │ ──────────────────> │ crash_server.py  │
│ 生成 dmp  │                      │ :8080            │
│ 自动上报  │ <────────────────── │ 返回分析结果      │
└──────────┘    JSON 结果         └──────────────────┘
```

```bash
# 服务器启动
python crash_server.py --port 8080 --search-dir build/bin/RelWithDebInfo

# 客户端配置（C++ 代码中）
INIT_CRASH_HANDLER(L"http://192.168.1.100:8080");
```

---

### 方案 4：CI/CD 自动创建 Issue

```yaml
# GitHub Actions 示例
name: Crash Analysis
on:
  repository_dispatch:
    types: [crash-report]

jobs:
  analyze:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install tools
        run: |
          sudo apt-get update
          sudo apt-get install -y minidump-stackwalk

      - name: Download dump
        run: curl -o crash.dmp "${{ github.event.client_payload.dump_url }}"

      - name: Analyze
        run: |
          minidump_stackwalk crash.dmp ./symbols/ > report.txt 2>&1 || true
          python -c "
          import json, sys
          data = json.load(open('crash.dmp.json'))
          summary = f\"Crash: {data.get('crash_reason', 'unknown')}\"
          print(summary)
          " > title.txt

      - name: Create Issue
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('report.txt', 'utf8');
            const title = fs.readFileSync('title.txt', 'utf8').trim();
            await github.rest.issues.create({
              owner: context.repo.owner,
              repo: context.repo.repo,
              title: title,
              body: `## Crash Analysis\n\n\`\`\`\n${report}\n\`\`\``,
              labels: ['crash', 'auto-reported']
            });
```

---

## 环境部署指南

### Windows 开发环境

#### 1. Python 环境

```powershell
# 检查 Python 版本（需要 3.8+）
python --version

# 如果没有 Python，从官网下载安装：
# https://www.python.org/downloads/
# 安装时勾选 "Add Python to PATH"
```

#### 2. 安装符号化工具（LLVM）

```powershell
# 方式 1：winget 安装（推荐）
winget install LLVM.LLVM

# 方式 2：手动下载
# https://github.com/llvm/llvm-project/releases
# 下载 LLVM-xxx-win64.exe 安装

# 验证安装
llvm-symbolizer --version
```

#### 3. 配置环境变量

```powershell
# 将 Tools 目录加入 PATH（可选）
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Users\xx\Documents\Cpp\CAD\Tools", "User")

# 设置 PYTHONIOENCODING 解决中文乱码
[Environment]::SetEnvironmentVariable("PYTHONIOENCODING", "utf-8", "User")
```

#### 4. 验证安装

```powershell
cd C:\Users\xx\Documents\Cpp\CAD\Tools

# 测试分析脚本
python crash_analyze.py --help

# 测试分析一个 dmp 文件
python crash_analyze.py your_crash.dmp --no-color

# 测试服务器启动
python crash_server.py --port 8080
# 然后浏览器访问 http://localhost:8080/health
```

---

### Linux 服务器部署

#### 1. 安装依赖

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install python3 python3-pip nginx build-essential cmake git -y

# CentOS / RHEL
sudo yum install python3 python3-pip nginx gcc-c++ cmake git -y
```

#### 2. 构建 Breakpad 工具链（minidump_stackwalk + dump_syms）

```bash
# 获取源码
cd /opt
git clone https://chromium.googlesource.com/breakpad/breakpad
cd breakpad
git clone https://chromium.googlesource.com/breakpad/src/third_party/lss lss

# 构建
mkdir -p out && cd out
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc) --target minidump_stackwalk dump_syms

# 安装到系统路径
sudo cp minidump_stackwalk /usr/local/bin/
sudo cp dump_syms /usr/local/bin/

# 验证
minidump_stackwalk --help
dump_syms --help
```

#### 3. 部署脚本和符号目录

```bash
# 创建部署目录
sudo mkdir -p /opt/crash-analyzer
sudo mkdir -p /opt/crash-symbols
sudo mkdir -p /var/log/crash-analyzer
sudo mkdir -p /var/crash-dumps

# 复制脚本
cp crash_analyze.py crash_server.py crash_reporter.py /opt/crash-analyzer/
chmod +x /opt/crash-analyzer/*.py

# 创建专用用户
sudo useradd -r -s /bin/false crash-analyzer
sudo chown -R crash-analyzer:crash-analyzer /opt/crash-analyzer
sudo chown -R crash-analyzer:crash-analyzer /opt/crash-symbols
sudo chown -R crash-analyzer:crash-analyzer /var/log/crash-analyzer
sudo chown -R crash-analyzer:crash-analyzer /var/crash-dumps
```

#### 4. 创建 systemd 服务

```bash
sudo tee /etc/systemd/system/crash-analyzer.service << 'EOF'
[Unit]
Description=Crash Analyzer HTTP Server
After=network.target

[Service]
Type=simple
User=crash-analyzer
Group=crash-analyzer
WorkingDirectory=/opt/crash-analyzer
ExecStart=/usr/bin/python3 /opt/crash-analyzer/crash_server.py \
    --port 8080 \
    --stackwalk /usr/local/bin/minidump_stackwalk \
    --symbol-root /opt/crash-symbols
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

# 安全限制
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/crash-dumps /var/log/crash-analyzer /opt/crash-symbols

# 资源限制
LimitNOFILE=65536
MemoryMax=512M

[Install]
WantedBy=multi-user.target
EOF
```

#### 4. 启动服务

```bash
# 重载 systemd
sudo systemctl daemon-reload

# 启动服务
sudo systemctl start crash-analyzer

# 设置开机自启
sudo systemctl enable crash-analyzer

# 查看状态
sudo systemctl status crash-analyzer

# 查看日志
sudo journalctl -u crash-analyzer -f
```

#### 5. 配置 Nginx 反向代理

```bash
sudo tee /etc/nginx/sites-available/crash-analyzer << 'EOF'
server {
    listen 80;
    server_name crash.your-domain.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl http2;
    server_name crash.your-domain.com;

    ssl_certificate /etc/ssl/certs/crash-analyzer.crt;
    ssl_certificate_key /etc/ssl/private/crash-analyzer.key;

    # 上传限制（dmp 文件可能很大）
    client_max_body_size 100m;

    # 超时设置
    proxy_read_timeout 60s;
    proxy_send_timeout 60s;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # 健康检查（不记录日志）
    location = /health {
        proxy_pass http://127.0.0.1:8080;
        access_log off;
    }
}
EOF

# 启用站点
sudo ln -s /etc/nginx/sites-available/crash-analyzer /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

#### 6. Docker 部署（推荐生产环境）

**Dockerfile：**

```dockerfile
FROM ubuntu:22.04

# 安装依赖
RUN apt-get update && apt-get install -y \
    python3 python3-pip \
    build-essential cmake git \
    && rm -rf /var/lib/apt/lists/*

# 构建 Breakpad 工具链
RUN cd /tmp && \
    git clone https://chromium.googlesource.com/breakpad/breakpad && \
    cd breakpad && \
    git clone https://chromium.googlesource.com/breakpad/src/third_party/lss lss && \
    mkdir -p out && cd out && \
    cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . -j$(nproc) --target minidump_stackwalk dump_syms && \
    cp minidump_stackwalk dump_syms /usr/local/bin/ && \
    rm -rf /tmp/breakpad

WORKDIR /app
COPY crash_analyze.py crash_server.py ./

# 符号目录（挂载卷）
VOLUME ["/opt/crash-symbols"]

EXPOSE 8080
CMD ["python3", "crash_server.py", "--port", "8080", "--stackwalk", "/usr/local/bin/minidump_stackwalk"]
```

**docker-compose.yml：**

```yaml
version: '3.8'

services:
  crash-analyzer:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - crash-symbols:/opt/crash-symbols     # 符号文件
      - crash-dumps:/var/crash-dumps          # 崩溃报告
      - crash-logs:/var/log/crash-analyzer    # 日志
    restart: unless-stopped
    deploy:
      resources:
        limits:
          memory: 512M
          cpus: '1.0'

volumes:
  crash-symbols:
    driver: local
    driver_opts:
      type: none
      o: bind
      device: /path/to/your/symbols
  crash-dumps:
  crash-logs:
```

**启动：**

```bash
# 构建并启动
docker-compose up -d

# 查看日志
docker-compose logs -f

# 符号更新（重新同步后重启）
docker-compose restart crash-analyzer
```

---

### Windows Server 部署

#### 1. 安装 Python 和依赖

```powershell
# 下载 Python 安装包
# https://www.python.org/downloads/
# 安装时勾选 "Install for all users" 和 "Add Python to PATH"

# 验证
python --version
pip --version
```

#### 2. 创建 Windows 服务

使用 NSSM (Non-Sucking Service Manager) 将 crash_server.py 注册为 Windows 服务：

```powershell
# 下载 NSSM
# https://nssm.cc/download
# 解压后将 nssm.exe 放到 PATH 目录

# 注册服务
nssm install CrashAnalyzer "C:\Python3x\python.exe"
nssm set CrashAnalyzer AppParameters "C:\Tools\crash_server.py --port 8080"
nssm set CratAnalyzer AppDirectory "C:\Tools"
nssm set CrashAnalyzer DisplayName "Crash Analyzer Server"
nssm set CrashAnalyzer Description "SanYiCAD 崩溃分析 HTTP 服务器"
nssm set CrashAnalyzer Start SERVICE_AUTO_START

# 启动服务
nssm start CrashAnalyzer

# 查看状态
nssm status CrashAnalyzer
```

#### 3. 配置 Windows 防火墙

```powershell
# 允许 8080 端口入站
netsh advfirewall firewall add rule name="Crash Analyzer HTTP" ^
    dir=in action=allow protocol=tcp localport=8080

# 如果使用 nginx，开放 443 端口
netsh advfirewall firewall add rule name="HTTPS" ^
    dir=in action=allow protocol=tcp localport=443
```

#### 4. 使用 IIS 反向代理（可选）

如果服务器已有 IIS，可以使用 ARR (Application Request Routing) 反向代理：

```powershell
# 安装 ARR
Install-WindowsFeature Web-Server -IncludeManagementTools
Install-WindowsFeature Web-Url-Auth
Install-WindowsFeature Web-Request-Monitor

# 在 IIS 管理器中配置：
# 1. 服务器代理设置 -> 启用代理
# 2. 添加 URL 重写规则：
#    模式: (.*)
#    动作: 重写 -> http://localhost:8080/{R:1}
```

---

### HTTPS 配置

#### 自签名证书（开发/测试）

```bash
# Linux
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout /etc/ssl/private/crash-analyzer.key \
    -out /etc/ssl/certs/crash-analyzer.crt \
    -subj "/C=CN/ST=Beijing/L=Beijing/O=SanYiCAD/CN=crash.your-domain.com"

# Windows (PowerShell)
New-SelfSignedCertificate `
    -DnsName "crash.your-domain.com" `
    -CertStoreLocation "Cert:\LocalMachine\My" `
    -NotAfter (Get-Date).AddYears(1)
```

#### Let's Encrypt（生产环境）

```bash
# 安装 certbot
sudo apt install certbot python3-certbot-nginx -y

# 获取证书
sudo certbot --nginx -d crash.your-domain.com

# 自动续期（certbot 会自动配置 cron/systemd timer）
sudo certbot renew --dry-run
```

---

### 进程管理

#### 查看日志

```bash
# Linux (systemd)
sudo journalctl -u crash-analyzer -f              # 实时日志
sudo journalctl -u crash-analyzer --since today    # 今天的日志
sudo journalctl -u crash-analyzer -n 100           # 最后100行

# Windows (NSSM)
# 日志默认在 NSSM 安装目录的 logs 子目录
# 或者查看事件查看器 -> Windows 日志 -> 应用程序
```

#### 重启服务

```bash
# Linux
sudo systemctl restart crash-analyzer

# Windows
nssm restart CrashAnalyzer
```

#### 查看端口占用

```bash
# Linux
sudo ss -tlnp | grep 8080
sudo lsof -i :8080

# Windows
netstat -ano | findstr :8080
```

---

### 防火墙配置

#### Linux (ufw)

```bash
# 允许 HTTP/HTTPS
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# 限制 8080 端口只允许内网访问
sudo ufw allow from 192.168.0.0/16 to any port 8080

# 启用防火墙
sudo ufw enable
sudo ufw status
```

#### Linux (firewalld)

```bash
sudo firewall-cmd --permanent --add-port=80/tcp
sudo firewall-cmd --permanent --add-port=443/tcp
sudo firewall-cmd --reload
```

#### Windows

```powershell
# 开放端口
netsh advfirewall firewall add rule name="Crash Analyzer" ^
    dir=in action=allow protocol=tcp localport=8080

# 删除规则
netsh advfirewall firewall delete rule name="Crash Analyzer"
```

---

### 监控与告警

#### 简单监控脚本

```bash
#!/bin/bash
# monitor.sh - 定期检查服务健康状态

URL="http://localhost:8080/health"
LOG="/var/log/crash-analyzer/monitor.log"

while true; do
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$URL")
    
    if [ "$HTTP_CODE" != "200" ]; then
        echo "[$(date)] Service DOWN (HTTP $HTTP_CODE)" >> "$LOG"
        # 发送告警（邮件/钉钉/企业微信）
        # curl -X POST "https://your-webhook-url" -d '{"msg":"Crash Analyzer is down!"}'
        
        # 尝试重启
        sudo systemctl restart crash-analyzer
    else
        echo "[$(date)] Service OK" >> "$LOG"
    fi
    
    sleep 60
done
```

#### Systemd Timer 自动监控

```bash
# 创建监控 service
sudo tee /etc/systemd/system/crash-analyzer-monitor.service << 'EOF'
[Unit]
Description=Crash Analyzer Health Check

[Service]
Type=oneshot
ExecStart=/opt/crash-analyzer/monitor.sh
EOF

# 创建 timer（每分钟执行）
sudo tee /etc/systemd/system/crash-analyzer-monitor.timer << 'EOF'
[Unit]
Description=Run Crash Analyzer monitor every minute

[Timer]
OnBootSec=1min
OnUnitActiveSec=1min

[Install]
WantedBy=timers.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now crash-analyzer-monitor.timer
```

#### 统计上传量

```bash
# 查看今天的请求数
sudo journalctl -u crash-analyzer --since today | grep "POST /analyze" | wc -l

# 查看平均响应时间
sudo journalctl -u crash-analyzer --since today | grep "analysis_time_ms" | \
    awk -F'"analysis_time_ms": ' '{print $2}' | awk '{sum+=$1; n++} END {print "Avg:", sum/n, "ms"}'
```

---

### 完整部署检查清单

```
□  Python 3.8+ 已安装
□  符号化工具已安装（llvm-symbolizer 或 addr2line）
□  crash_analyze.py 可正常运行
□  crash_server.py 可正常启动
□  防火墙已开放相应端口
□  Nginx/IIS 反向代理已配置（生产环境）
□  SSL 证书已配置（生产环境）
□  systemd/Windows 服务已注册
□  日志轮转已配置
□  监控告警已配置
□  备份策略已制定
```

#### 日志轮转配置

```bash
# 创建 logrotate 配置
sudo tee /etc/logrotate.d/crash-analyzer << 'EOF'
/var/log/crash-analyzer/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    create 0640 crash-analyzer crash-analyzer
    sharedscripts
    postrotate
        systemctl reload crash-analyzer > /dev/null 2>&1 || true
    endscript
}
EOF
```

#### 备份策略

```bash
# 每周备份崩溃报告和配置
sudo tee /etc/cron.weekly/crash-analyzer-backup << 'EOF'
#!/bin/bash
BACKUP_DIR="/backup/crash-analyzer"
DATE=$(date +%Y%m%d)

mkdir -p "$BACKUP_DIR"

# 备份配置
tar czf "$BACKUP_DIR/config-$DATE.tar.gz" \
    /opt/crash-analyzer/*.py \
    /etc/systemd/system/crash-analyzer*.service \
    /etc/nginx/sites-available/crash-analyzer 2>/dev/null

# 备份最近7天的崩溃报告（不备份 dmp 文件，太大）
tar czf "$BACKUP_DIR/reports-$DATE.tar.gz" \
    /var/log/crash-analyzer/*.log 2>/dev/null

# 保留最近30天的备份
find "$BACKUP_DIR" -name "*.tar.gz" -mtime +30 -delete
EOF

sudo chmod +x /etc/cron.weekly/crash-analyzer-backup
```

---

## FAQ

### Q: 为什么模块名称显示乱码？

A: 模块名称来自 minidump 中的 `MINIDUMP_STRING` 结构。如果 stride 探测选择了错误的记录大小，`module_name_rva` 可能指向垃圾数据。脚本会自动跳过无效名称，不影响崩溃地址匹配。

### Q: 如何支持 PDB 符号？

A: 使用 `--search-dir` 指定 PDB 文件所在目录。脚本会自动查找匹配的二进制文件进行符号化。

### Q: 支持 32 位程序的 dmp 吗？

A: 支持。脚本会自动检测 dump 的 CPU 架构（AMD64/ARM64/x86）并使用对应的寄存器布局和指针宽度。

### Q: 如何在生产环境部署服务器？

A: 建议使用 nginx 反向代理 + HTTPS，并添加认证 token 防止恶意上传。可以在 `crash_server.py` 中添加 token 验证逻辑。

### Q: 崩溃 PC 显示 "未匹配模块" 怎么办？

A: 说明模块列表解析失败。尝试：
1. 使用 `--dump-stackwalk` 指定 `minidump_stackwalk`
2. 检查 dmp 文件是否完整
3. 使用 `--raw` 查看模块列表原始数据
