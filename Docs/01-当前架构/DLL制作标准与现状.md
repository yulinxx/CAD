# DLL 开发标准与现状

> **本文档合并自 `DLL制作标准与现状.md` + `DLL说明.md`（2026-08-11）。**
> 
> **文档定位**：
> 
> - **第一部分：DLL 开发标准** —— 任何 DLL 开发都必须遵守的设计规范（接口、内存、ABI、导出宏、修复模式等），含从 `DLL说明.md` 吸收的补充审计维度。
> - **第二部分：现有 DLL 问题与收口现状** —— 工程各模块的 ABI 违规清单、批次收口记录、迁移路线与最终状态。
> 
> **使用方式**：开发/新增 DLL 前读第一部分；评估/改造现有 DLL 时对照第二部分。
> 
> ⚠️ **本文档已结合工程实际代码审计修正，与 `C:\Users\xx\Documents\Cpp\CAD` 工程现状对齐。**
> 建议后续所有 DLL 相关的改动先更新本文档，再改代码。

---

## 目录

- **第一部分：DLL 开发标准**
  - 前言：本文档的地位
  - 一、通用原则（一～九节）
  - 一~十一：接口设计、内存、异常、ABI、线程、构建、版本、调试、安全、检查表、STL 修复模式
  - 十二、结合当前 CAD 工程的 DLL 划分建议
  - 十八~二十四：导出宏、构建体系、插件、接口范式、平台、发布清单、最终原则
  - 二十五、编译器与符号可见性规范
  - 二十六、内存分配/释放风险模式
  - 二十七、虚函数与 RTTI 规范
  - 二十八、模板类与内联函数规范
  - 二十九、静态成员与全局状态规范
  - 三十、Include 目录组织规范
- **第二部分：现有 DLL 问题与收口现状**
  - 全局状态
  - 十三、2026-07-30 STL / ABI 跨 DLL 审计结论
  - 三十一、跨 DLL STL 违规清单（全模块详细清单）
  - 三十二、分批收口记录
  - 三十三、UI2D / UI3D ABI 分批收口清单（实施计划）
  - 三十四、落地检查清单
  - 三十五、当前工程的模块迁移表
  - 三十六、P0 必修复项的改造路线图
  - 三十七、当前仍存在的风险项

---

## 第一部分：DLL 开发标准

> 本节是任何 DLL 项目都应遵守的设计规范。开发新 DLL 或修改导出接口前，先对照本节。

---

## 前言：本文档的地位

本文档是 SanYi CAD 工程的 **DLL / ABI 设计标准**，覆盖从设计到部署的全生命周期  
所有涉及以下内容的改动，必须先对照本文档：

- 新增 / 拆分 DLL
- 对外导出函数或头文件
- 插件接口
- 脚本 / 语言绑定接口
- 跨平台发布
- 版本升级与兼容性修复
- 弃用旧接口

---

## 一、通用原则（一～九节）

以下原则**不区分工程**，任何 DLL 项目都应遵守。

---

---

## 一、接口设计原则

| # | 原则 | 说明 |
|---|------|------|
| 1 | **优先 C 接口** | 导出函数用 `extern "C"`，避免 C++ name mangling |
| 2 | **纯虚接口类** | 若必须用 C++ 类，仅导出纯虚基类指针，实现隐藏在 DLL 内 |
| 3 | **禁止导出 STL** | 公共接口不出现 `std::string`/`vector`/`map`，用 `char*` + 长度、`const T*` + `size_t` 替代 |
| 4 | **禁止导出模板** | 模板类/函数不要加 `__declspec(dllexport)` |
| 5 | **POD 结构体传输** | 跨 DLL 传递的数据用简单结构体，确保无指针、无虚函数、无引用 |
| 6 | **版本号机制** | 提供 `GetVersion()` 函数，主版本号变化 = ABI 不兼容 |
| 7 | **前向兼容设计** | 结构体末尾预留 `reserved` 字段，未来扩展不破坏布局 |

---

---

## 二、内存管理

| # | 规则 | 说明 |
|---|------|------|
| 8 | **谁分配谁释放** | DLL 内 `new` 的对象必须在 DLL 内 `delete` |
| 9 | **工厂 + Release 模式** | 提供 `CreateXxx()` 和 `DestroyXxx()`，禁止客户端直接 delete |
| 10 | **禁止跨 DLL new/delete** | 不同 CRT（`/MD` vs `/MT`）的堆管理器不同，混用必崩溃 |
| 11 | **引用计数** | 多客户端共享对象时用 `AddRef()`/`Release()`（类似 COM） |
| 12 | **数组分配一致性** | 若返回数组，同时提供 `FreeArray(void* ptr)`，禁止客户端 `free()` |
| 13 | **避免裸指针所有权歧义** | 函数参数用 `const T*`（借用）或 `T**`（输出），语义明确 |

---

---

## 三、异常与错误处理

| # | 规则 | 说明 |
|---|------|------|
| 14 | **DLL 内部 catch 所有异常** | 导出函数入口包 `try/catch(...)`，绝不让异常跨 DLL 边界 |
| 15 | **错误码枚举** | 返回 `int` 或枚举错误码，不要用 C++ 异常作为接口错误机制 |
| 16 | **线程局部错误信息** | 提供 `GetLastErrorMessage(char* buf, size_t len)`，线程安全 |
| 17 | **参数校验前置** | 每个导出函数第一行检查空指针、非法值，尽早返回错误 |
| 18 | **断言仅内部使用** | `assert()` 只在 Debug 版生效，Release 版必须返回错误码 |

---

---

## 四、ABI 兼容性

| # | 规则 | 说明 |
|---|------|------|
| 19 | **编译器锁定** | 同一套 DLL 必须用相同编译器版本、相同 `/MD` 或 `/MT` 配置 |
| 20 | **结构体对齐控制** | 必要时用 `#pragma pack(push, 8)` 固定对齐，避免不同编译器默认差异 |
| 21 | **禁用虚继承** | 虚基类布局编译器相关，跨 DLL 风险极高 |
| 22 | **禁用 RTTI 跨 DLL** | `dynamic_cast` 依赖编译器实现，跨 DLL 可能失效 |
| 23 | **禁用异常规格** | `throw()` 在旧编译器上影响符号导出 |
| 24 | **枚举底层类型固定** | `enum class ErrorCode : int32_t { ... }`，防止编译器选不同宽度 |

---

---

## 五、线程安全

| # | 规则 | 说明 |
|---|------|------|
| 25 | **全局状态线程保护** | 单例、缓存、日志等用 `std::mutex` 或 `std::atomic` |
| 26 | **回调线程约定** | 文档明确回调在哪个线程调用（工作线程？ UI 线程？） |
| 27 | **线程局部存储慎用** | `thread_local` 在 DLL 卸载时可能不调用析构（Windows 已知问题） |
| 28 | **初始化顺序** | 避免跨 DLL 的全局变量依赖，用显式 `Initialize()`/`Shutdown()` |
| 29 | **DLL 卸载安全** | 确保所有线程已退出再 `FreeLibrary`，防止回调到已卸载代码 |

---

---

## 六、构建与部署

| # | 规则 | 说明 |
|---|------|------|
| 30 | **符号可见性控制** | Windows 用 `__declspec(dllexport)` 或 `.def` 文件；Linux 用 `-fvisibility=hidden` + `__attribute__((visibility("default")))` |
| 31 | **导出符号最小化** | 只导出必要符号，减少攻击面和版本锁定风险 |
| 32 | **PDB 分离** | Release 版 PDB 单独存放，不随安装包分发 |
| 33 | **清单文件（Windows）** | 嵌入 manifest 指定 CRT 版本，避免"缺少 MSVCP140.dll" |
| 34 | **依赖检查脚本** | 构建后运行 `dumpbin /dependents` 或 `ldd`，确认无意外依赖 |
| 35 | **side-by-side 部署** | 将 CRT 运行时与 DLL 放同一目录，避免依赖系统全局版本 |

---

---

## 七、版本与升级

| # | 规则 | 说明 |
|---|------|------|
| 36 | **语义化版本** | `MAJOR.MINOR.PATCH`，MAJOR 变 = 不兼容，MINOR 变 = 功能新增 |
| 37 | **版本检测函数** | 客户端启动时调用 `GetVersion()`，不匹配则拒绝加载 |
| 38 | **结构体大小校验** | 接口接收结构体时检查 `sizeof(YourStruct) == expectedSize` |
| 39 | **功能探测接口** | 提供 `HasFeature(const char* name)`，客户端优雅降级 |
| 40 | **旧版本保留** | 新 DLL 同时保留旧导出函数（内部转发到新实现），过渡至少一个大版本 |

---

---

## 八、调试与诊断

| # | 规则 | 说明 |
|---|------|------|
| 41 | **日志接口导出** | 提供 `SetLogCallback(void(*)(int level, const char* msg))` |
| 42 | **内存泄漏检测** | Debug 版导出 `GetAllocationStats()`，返回内部 new/delete 计数 |
| 43 | **句柄校验** | 每个接口函数校验传入的句柄/指针是否属于本 DLL 实例 |
| 44 | **边界检查** | 数组访问、字符串拷贝强制带长度限制，防溢出 |
| 45 | **死锁检测** | Debug 版锁实现加入超时和 owner 线程记录 |

---

---

## 九、安全

| # | 规则 | 说明 |
|---|------|------|
| 46 | **输入消毒** | 字符串参数限制长度，路径参数禁止 `..` 遍历 |
| 47 | **缓冲区大小双重校验** | 输出缓冲区参数同时传 `buffer` 和 `bufferSize`，内部严格检查 |
| 48 | **禁用危险函数** | 不用 `strcpy`、`sprintf`，用 `strncpy_s`、`snprintf` |
| 49 | **DLL 劫持防护** | 使用绝对路径 `LoadLibraryExW` + `LOAD_WITH_ALTERED_SEARCH_PATH` |
| 50 | **代码签名** | 发布前对 DLL 进行数字签名，防篡改 |

---

---

## 十、快速检查表（开发时逐项打勾）

```
□ 公共接口无 STL
□ 所有导出函数 extern "C"
□ 内存分配/释放在同一模块
□ 异常不跨 DLL 边界
□ 结构体有固定对齐和大小
□ 线程安全已考虑
□ 版本号可查询
□ 错误码完整覆盖
□ 符号导出最小化
□ 依赖项已审计
□ 有示例程序和测试程序
```

---

---

## 十一、STL 跨边界问题修复模式（实践总结）

> ⚠️ **本节根据 Log.dll 和 GeoModelCore.dll 的修复经验新增** —— 记录已验证的修复模式。

### 模式 1：两层结构分离（推荐用于配置结构体）

**适用场景**：导出的结构体需要包含字符串或其他 STL 类型

**做法**：
1. 创建内部版本 `XxxInternal`，使用 `std::string`
2. 创建导出版本 `Xxx`，使用 `const char*`
3. 提供类型转换运算符

**示例**（Log.dll - SyLogConfig）：

```cpp
// 内部版本（不导出）
struct SyLogConfigInternal {
    std::string logName = "SanYi";
    std::string logPath = "";
    // ... 其他成员
};

// 导出版本（LOG_API）
struct LOG_API SyLogConfig {
    const char* logName = "SanYi";
    const char* logPath = "";
    // ... 其他成员
    
    operator SyLogConfigInternal() const {
        SyLogConfigInternal cfg;
        if (logName) cfg.logName = logName;
        if (logPath) cfg.logPath = logPath;
        // ...
        return cfg;
    }
};
```

**优点**：完全隔离 STL，内部实现不受影响

---

### 模式 2：内联函数兼容（推荐用于方法重载）

**适用场景**：导出类的方法需要同时支持 `const char*` 和 `std::string`

**做法**：
1. 导出 `const char*` 版本的方法
2. 添加 `inline` 的 `std::string` 重载版本（定义在头文件中）

**示例**（Log.dll - SyLogger）：

```cpp
class LOG_API SyLogger {
public:
    // 导出的 const char* 版本
    void LogSrc(SyLogLevel level, const char* file, int line, const char* msg);
    
    // 内联的 std::string 版本（不导出，编译器在调用处展开）
    inline void LogSrc(SyLogLevel level, const char* file, int line, const std::string& msg) {
        LogSrc(level, file, line, msg.c_str());
    }
};
```

**关键要点**：
- `inline` 函数不会被导出到 DLL
- 编译器在调用处直接展开，`std::string` 对象只存在于调用方编译单元
- 不涉及 STL 对象跨越 DLL 边界

---

### 模式 3：固定缓冲区（推荐用于状态/错误信息）

**适用场景**：导出的结构体需要返回字符串信息

**做法**：
1. 使用固定大小的 `char[]` 数组替代 `std::string`
2. 使用 `std::strncpy` 确保安全

**示例**（GeoModelCore - GmcStatus）：

```cpp
struct GEOMODEL_API GmcStatus {
    bool ok = true;
    GmcErrorCode code = GmcErrorCode::Success;
    char message[256] = {0};  // 固定缓冲区
    
    static GmcStatus fail(GmcErrorCode code, const char* msg) {
        GmcStatus s;
        s.ok = false;
        s.code = code;
        if (msg)
            std::strncpy(s.message, msg, sizeof(s.message) - 1);
        return s;
    }
};
```

**优点**：简单直接，无需内存管理

---

### 模式对比

| 模式 | 适用场景 | 复杂度 | 风险 |
|---|---|---|---|
| 两层结构分离 | 配置结构体 | 中等 | 低 |
| 内联函数兼容 | 方法重载 | 低 | 低（内联不导出） |
| 固定缓冲区 | 状态/错误信息 | 低 | 低 |
| 析构函数分离 | 导出类含 STL 成员 | 低 | 低（析构在 DLL 内完成） |
| 裸指针 PIMPL | 导出类用 `unique_ptr<Impl>` | 中等 | 低（同 CRT 下安全） |
| `#pragma warning(disable : 4251)` | ❌ 不推荐 | 低 | **高**（掩盖问题） |
| KvPair + 回调迭代器 | 纯虚接口替代 `std::map`/`std::vector` | 中高 | 低（纯 POD 跨边界） |

---

### 模式 4：KvPair + 回调迭代器（推荐用于替代 std::map/std::vector 纯虚接口）

**适用场景**：纯虚接口中需要传递键值对集合或返回查询结果集

**问题**：`std::map<std::string, std::string>` 和 `std::vector<std::map<...>>` 在虚函数中跨 DLL 边界传递时，不同 CRT 编译的客户端会崩溃

**做法**：
1. 定义 POD 键值对结构体 `KvPair`（`const char* key, const char* value`）
2. 定义 C 函数指针回调 `QueryRowCallback` 替代 `std::vector` 返回值
3. 接口方法改为 `const KvPair* values, size_t count` 替代 `const std::map&`

**示例**（UICommon - IDatabase）：

```cpp
// POD 键值对，替代 std::map<std::string, std::string> 跨 DLL 边界
struct KvPair
{
    const char* key;
    const char* value;
};

// 查询结果行回调，替代 std::vector<std::map<std::string, std::string>> 返回
typedef void (*QueryRowCallback)(const KvPair* columns, size_t columnCount, void* userData);

class UICOMMON_API IDatabase
{
public:
    virtual ~IDatabase();  // 析构移至 .cpp

    // KvPair 数组替代 std::map
    virtual bool insert(const char* tableName, const KvPair* values, size_t count) = 0;

    // 回调替代 std::vector 返回
    virtual void query(const char* sql, QueryRowCallback callback, void* userData,
        const char* const* params = nullptr, size_t paramCount = 0) = 0;

    // const char* 替代 const std::string&
    virtual const char* lastError() const = 0;
};
```

**调用方适配**（SettingsRepositoryImpl）：

```cpp
// 旧代码：返回 std::vector<std::map<std::string, std::string>>
const auto rows = m_database->query(sql.c_str());
for (const auto& row : rows) { ... }

// 新代码：回调模式
static void loadTableCallback(const UI::KvPair* columns, size_t columnCount, void* userData) {
    auto* result = static_cast<SettingsTable*>(userData);
    // 遍历 columns 处理每列...
}
m_database->query(sql.c_str(), loadTableCallback, &result);
```

```cpp
// 旧代码：std::map<std::string, std::string> row;
std::map<std::string, std::string> row;
row["key"] = key; row["value"] = valueStr;
m_database->insert(tableName, row);

// 新代码：KvPair 数组
UI::KvPair row[3] = { {"key", key.c_str()}, {"value", valueStr.c_str()}, {"type", "string"} };
m_database->insert(tableName, row, 3);
```

**优点**：完全消除 STL 跨 DLL 边界，所有参数为纯 POD 类型

**已应用的类**：
- `IDatabase`、`IBusinessDataRepository`、`DatabaseWrapper`、`BusinessDataRepository`、`SettingsRepositoryImpl`

---

### 模式 5：析构函数分离（推荐用于导出类含 STL 成员）

**适用场景**：导出类（带 `XXX_API`）包含 `std::string`、`std::vector`、`std::unique_ptr` 等 STL 成员

**问题**：MSVC C4251 警告：`class XXX_API Foo` 含 STL 成员时，客户端若用不同 CRT 编译会崩溃

**做法**：
1. 头文件中**声明**析构函数（不要 `= default`）
2. .cpp 文件中**定义**析构函数（`= default` 即可）
3. 移动构造/赋值同理，从 `= default` 移至 .cpp

**示例**（Engine2D - SySmartLine）：

```cpp
// SySmartLine.h
struct ENGINE2D_API SySmartLine : public SyEntity
{
    SySmartLine(const SySmartLine& other);
    SySmartLine(SySmartLine&& other) noexcept;           // 声明，不 = default
    SySmartLine& operator=(const SySmartLine& other);
    SySmartLine& operator=(SySmartLine&& other) noexcept; // 声明，不 = default
    virtual ~SySmartLine();                               // 声明，不 = default

private:
    std::vector<std::unique_ptr<SyEntity>> m_vSegments;   // STL 成员
};

// SySmartLine.cpp
SySmartLine::SySmartLine(SySmartLine&& other) noexcept
{
    m_vSegments = std::move(other.m_vSegments);
    // ...
}
SySmartLine::~SySmartLine() = default;  // 在 DLL 内销毁 STL 成员
```

**关键要点**：
- 析构函数在 DLL 内执行，STL 成员的释放（`vector::~vector()`、`unique_ptr::~unique_ptr()`）调用的是 DLL 内的 CRT
- 客户端代码销毁 `SySmartLine` 对象时，调用的是导出的析构符号，而非内联展开
- 移动操作同理：`std::move` 涉及 STL 内部状态，必须在 DLL 内完成

**已应用的类**：
- `SyEntity`、`IEntity`、`ISceneManager`、`TextItem`、`IUndoRedoCommand` ✅ 均已修复
- `SySmartLine`、`SyLayer`、`TextConverter`、`UndoRedoManager::Command` ✅ 均已修复
- `IUndoRedoManager`、`ILayerManager`、`SceneRenderContract`（ISceneGeometrySink/ISceneDataSource） ✅ 均已修复
- `IFileParser`、`IFileWriter`、`ISyCryptoProvider`、`IDatabase`、`IBusinessDataRepository` ✅ 均已修复

---

### 模式 5：裸指针 PIMPL（推荐用于 `unique_ptr<Impl>` 导出类）

**适用场景**：导出类使用 `std::unique_ptr<Impl>` 做 PIMPL，触发 C4251

**问题**：`std::unique_ptr<forward-declared-type>` 在导出类中无法安全跨 DLL 销毁

**做法**：
1. 将 `std::unique_ptr<Impl> m_impl` 改为 `Impl* m_impl`（裸指针）
2. 构造函数中 `m_impl = new Impl();`
3. 析构函数中 `delete m_impl; m_impl = nullptr;`
4. 拷贝/移动操作手动管理指针

**示例**（GeoModelCore - TopoShape）：

```cpp
// TopoShape.h
class GEOMODEL_API TopoShape
{
public:
    TopoShape();
    TopoShape(const TopoShape& other);
    TopoShape& operator=(const TopoShape& other);
    ~TopoShape();

private:
    // 裸指针 PIMPL：避免 std::unique_ptr<forward-declared> 跨 DLL 边界
    TopoShapeImpl* m_impl;
};

// TopoShape.cpp
TopoShape::TopoShape() : m_impl(new TopoShapeImpl()) {}
TopoShape::~TopoShape() { delete m_impl; m_impl = nullptr; }
TopoShape::TopoShape(const TopoShape& other)
    : m_impl(other.m_impl ? other.m_impl->clone().release() : new TopoShapeImpl()) {}
```

**关键要点**：
- `clone()` 仍返回 `std::unique_ptr<Impl>`，调用 `.release()` 提取裸指针
- 析构在 DLL 内 `delete`，确保调用 DLL 内的 `Impl::~Impl()`
- 完全消除 C4251 警告，无需任何 `#pragma warning(disable)`

**已应用的类**：
- `TopoShape`、`GeoModel`、`GmcBvh`、`GmcCurve`、`GmcSurface`（✅ **已标注为内部 C++ ABI 层** — TopoShape 析构已分离至 .cpp，unique_ptr 在完整类型可见处析构安全；STL 方法仅限内部使用）
- `SyLogger`（Log 模块 ✅ 已修复）

---

---

## 十二、结合当前 CAD 工程的 DLL 划分建议

> ⚠️ **本节已根据工程实际代码结构重写**。文档中的模块名均为实际 CMake 目标名。

### 1. 当前工程的真实模块全景

当前工程（SanYiCAD v1.0.0）共有 **19 个共享库**、**1 个可执行文件**、**1 个 Python 模块**：

| 层级 | 实际 CMake 目标 | 类型 | 导出宏 | C ABI Facade | 导出严格度 |
|---|---|---|---|---|---|
| **Core** | `Utility` | SHARED | `UTILITY_API` | 无 | ⭐ |
| | `Log` | SHARED | `LOG_API` | 有（`SyLoggerDLL.h`） | ⭐⭐⭐ |
| | `CrashHandler` | SHARED | `CRASHHANDLER_API` | 有（`CrashHandlerDLL.h`） | ⭐⭐⭐ |
| | `License` | SHARED | `LICENSE_API` | 有（`LicenseDLL.h`） | ⭐⭐⭐ |
| **Engine** | `EngineCommon` | SHARED | `ENGINE_API` | 部分（仅版本查询） | ⭐ |
| | `Engine2D` | SHARED | `ENGINE2D_API` | 无 | ⭐ |
| | `Engine3D` | SHARED | `ENGINE3D_API` | 无 | ⭐ |
| | `EnginePersistence` | SHARED | `ENGINEPERSISTENCE_EXPORTS` | 无 | 内部 |
| **Render** | `SanYiRender`（Renderx） | SHARED | `RENDER_API` | 有（`render/render.h`） | ⭐⭐⭐ |

| **UI** | `UICommon` | SHARED | `UICOMMON_API` | 无 | ⭐ |
| | `UI2D` | SHARED | `UI2D_API` | 无 | ⭐ |
| | `UI3D` | SHARED | `UI3D_API` | 无 | ⭐ |
| **功能模块** | `FileIO` | SHARED | `FILEIO_API` | 有（`FileIOExport.h`） | ⭐⭐⭐ |
| | `Nesting` | SHARED | `NESTING_API` / `NESTING_C_API` | 有（`NestingDLL.h`） | ⭐⭐⭐ |
| | `Hardware` | SHARED | `HARDWARE_API` | 无 | ⭐ |
| | `Network` | SHARED | `NETWORK_API` | 无 | ⭐ |
| | `Vision` | SHARED | `VISION_API` / `VISION_C_API` | 有（`VisionDLL.h`） | ⭐⭐⭐ |
| | `Engraving` | SHARED | `ENGRAVING_API` | 有（`EngravingCAPI.h`，类型安全句柄） | ⭐⭐⭐ |
| | `GeoModelCore` | SHARED | `GEOMODEL_API` | 有（`GeoModelDLL.h`） | ⭐⭐ |
| **Python** | `PythonHost` | SHARED | `PYTHONHOST_API` | 无 | ⭐ |
| | `SanYiPyBindCore` | MODULE (.pyd) | pybind11 处理 | 无（直接绑定 C++） | ⭐ |
| **入口** | `SanYiCAD` | EXECUTABLE | 不适用 | 不适用 | 不适用 |
| **工具** | `KeygenTool` | EXECUTABLE | 可选 | 不适用 | 不适用 |

**导出严格度说明**：
- ⭐⭐⭐ **严格导出**：提供完整的 C ABI facade，无 STL/Qt 跨边界，适合外部第三方调用
- ⭐⭐ **中等导出**：有 C ABI facade 但存在部分问题（如警告压制、void* 句柄等）
- ⭐ **宽松导出**：仅 C++ 接口导出，供内部同编译器模块使用，不保证跨编译器兼容
- **内部**：静态库或仅内部使用的模块，不对外暴露 ABI

### 2. 工程已存在的 C ABI Facade 全景

当前工程**已经有 11 套 C API facade**：

| 模块 | C API 文件 | 句柄风格 | 错误码 | 版本查询 | 状态 |
|---|---|---|---|---|---|
| **Log** | `SyLoggerDLL.h` | 无句柄（全局单例） | 无 | 有（`SyLog_GetVersion`） | ✅ 已完成 |
| **Engine** | `EngineAPI.h` | 无 | `int` | 有（`SanYiEngineVersion`） | ✅ 部分完成 |
| **FileIO** | `FileIOExport.h` | `struct FioManagerImpl*`（类型安全） | `FioResult`（含 `char[512]`） | 有（`fio_version`） | ✅ 已完成 |
| **Nesting** | `NestingDLL.h` | `typedef void* NestingJobHandle` | `enum NestingResultCode` | 有（`Nesting_GetVersion`） | ✅ 已完成 |
| **Vision** | `VisionDLL.h` | `typedef void* VisionImageHandle` | `enum VisionResultCode` | 有（`Vision_GetVersion`） | ✅ 已完成 |
| **GeoModelCore** | `GeoModelDLL.h` | `typedef void* GeoModelHandle` | `enum GeoModelResultCode` | 有（`GeoModel_GetVersion`） | ✅ 已完成 |
| **Engraving** | `EngravingCAPI.h` | `struct EngravingVolumeImpl*`（类型安全） | `enum EngravingResultCode` | 有（`Engraving_GetVersion`） | ✅ 已完成（2026-08-16 收口） |
| **License** | `LicenseDLL.h` | `struct LicenseContext*`（类型安全） | `enum LicenseResult` | 有（`License_GetVersion`） | ✅ 已完成 |
| **CrashHandler** | `CrashHandlerDLL.h` | 无句柄（全局单例） | `enum CrashHandlerResult` | 有（`CrashHandler_GetVersion`） | ✅ 已完成 |
| **SanYiRender** | `render/render.h` | `RenderDevice*`（类型安全不透明结构体指针） | 无（void 函数） | 无 | ✅ 已完成 |

**句柄风格说明**：
- ✅ **类型安全不透明指针**：`struct XxxImpl*` — 推荐，编译期类型检查
- ⚠️ **void* 句柄**：需运行时校验，易误用，建议逐步迁移到类型安全模式

### 3. 当前工程的分层与 ABI 边界建议

| 实际模块 | 建议形态 | 当前状态 | 导出严格度 |
|---|---|---|---|
| `SanYiCAD.exe` | **维持可执行文件** | 应用入口 + 装配 + 窗口启动 | 不适用 |
| `SanYiRender.dll` | **保持 C ABI facade** | 纯 C API（`render/render.h`），统一 2D/3D 渲染入口 | ⭐⭐⭐ |
| `Nesting.dll` | **保持 C ABI facade** | 完整 C API，需统一句柄风格 + 版本查询 | ⭐⭐⭐ |
| `Vision.dll` | **保持 C ABI facade** | 完整 C API，需统一句柄风格 + 版本查询 | ⭐⭐⭐ |
| `GeoModelCore.dll` | **保持 C ABI facade** | 已有 C API，需移除警告压制 + 版本查询 | ⭐⭐ |
| `Engraving.dll` | **保持 C ABI facade** | 已有 C API，需统一句柄风格 + 版本查询 | ⭐⭐⭐ |
| `Log.dll` | **保持 C ABI + C++ 双接口** | 已同时提供 C API 和 C++ 类，需补充版本查询 | ⭐⭐⭐ |
| `CrashHandler.dll` | **保持 C ABI facade** | 纯 C API，版本查询已完成 | ⭐⭐⭐ |
| `License.dll` | **保持 C ABI facade** | 完整 C API，版本查询已完成 | ⭐⭐⭐ |
| `FileIO.dll` | **C++ DLL + C ABI facade** | C API 已完成，需补充异常捕获 | ⭐⭐⭐ |
| `Engine*.dll` | **维持内部 C++ DLL** | 内部高效协作，不对外承诺 ABI | ⭐ |
| `UI*.dll` | **维持内部 C++ DLL** | Qt 界面层，不建议对外暴露 | ⭐ |
| `PythonHost.dll` | **维持内部 C++ DLL** | Python 集成框架 | ⭐ |
| `PyBindCore.pyd` | **考虑增加 C facade 层** | 当前直接绑定 C++，有 ABI 风险 | ⭐ |
| `Hardware.dll` | **维持内部 C++ DLL** | 激光硬件控制 | ⭐ |
| `Network.dll` | **维持内部 C++ DLL** | 网络通信 | ⭐ |
| `Utility.dll` | **维持内部 C++ DLL** | 工具库，无对外需求 | ⭐ |

### 4. 建议目标架构

保留当前**多 facade 模式**（每个模块各自导出 C ABI），而非合并为单一 `CADSDK.dll`。  
原因：各模块已有独立的 C API，强行合并会破坏现有调用方。

但需要**统一规范**这些已存在的 facade：

- 统一句柄风格 → 不透明结构体指针（`struct XxxImpl*`）
- 统一错误码 → `SanyiResult` 跨模块共用枚举
- 统一版本查询 → 每个 facade 提供 `Xxx_GetVersion()` 和 `Xxx_GetVersionString()`
- 统一导出宏模式 → `MODULE_API` + 头文件级别 `extern "C"` 包裹

### 5. 现有 C API 的不一致问题（需统一）

| 差异点 | Nesting | Vision | GeoModelCore | Engraving | SanYiRender | 建议统一方向 |
|---|---|---|---|---|---|---|---|
| 句柄风格 | `void*` | `void*` | `void*` | `void*` | `RenderDevice*`（类型安全） | **统一为不透明结构体指针** |
| 错误码 | `enum NestingResultCode` | `enum VisionResultCode` | `enum GeoModelResultCode` | `int` | 无 | **统一为 `SanyiResult`** |
| 导出宏 | `NESTING_C_API + NESTING_API` | `VISION_C_API` 含 extern "C" | `GEOMODEL_API` 纯 | `ENGRAVING_API` 纯 | `RENDER_API` 纯 | **统一为单体宏** |
| extern "C" 位置 | 头文件包裹 | 宏内自带 | 头文件包裹 | 头文件包裹 | 头文件包裹 | 统一为**头文件包裹** |
| 版本查询 | 有 | 有 | 有 | 有 | 无 | **每个 facade 增加** |
| 入参校验 | 有 | 有 | 有 | 有 | 有 | **所有导出函数前置校验** |
| 结构体大小校验 | 无 | 无 | 无 | 无 | 无 | **关键结构体增加 static_assert** |

### 7. 当前工程存在问题的 ABI 边界

以下位置**已违反 ABI 规范**，建议优先修复：

| 位置 | 问题 | 风险等级 | **状态** |
|---|---|---|---|
| `Log/SyLogger.h:18-50` | `SyLogConfig` 导出含 `std::string` 成员 → 已改为 `const char*` + 内部 `SyLogConfigInternal` | ✅ **已修复** | 用两层结构分离内部实现与导出接口 |
| `Log/SyLogger.h:52-126` | `SyLogger` 整类导出含 `std::unique_ptr`、`std::string` → 已改为 `const char*` 参数，`std::string` 重载为内联 | ✅ **已修复** | 内联函数不导出，避免 STL 跨边界 |
| `Log/SyLoggerDLL.h` | 缺少版本查询接口 | ✅ **已修复** | `SyLog_GetVersion()` 已补齐 |
| `GeoModelCore/GeoModelDLL.h:22-25` | 用 `#pragma warning(disable : 4251)` 压制而非修复 | ✅ **已修复** | 已移除压制；`TopoShape::dumpStl` 已改为 `const char*`；`makeFillet`/`makeChamfer`/`getAllFaces` 等仍有 `std::vector`（P1 内部使用可接受） |
| `GeoModelCore/GmcStatus.h:26` | `GmcStatus` 含 `std::string message` → 已改为 `char message[256]` | ✅ **已修复** | 固定缓冲区避免 STL 跨边界 |
| `GeoModelCore/GeoModelDLL.h` | ~~缺少版本查询接口~~ | ✅ **已修复** | `GeoModel_GetVersion()`/`GeoModel_GetVersionString()` 已在 `GeoModelImpl.cpp` 实现（与 Log/Nesting 模式一致） |
| `GeoModelCore/GmcTypes.h:147` | `GmcBooleanResult` 含 `std::string errorMessage` → 已改为 `char[256]` | ✅ **已修复** | 固定缓冲区避免 STL 跨边界；`GmcBooleanOps.cpp` 全部赋值改 `snprintf` |
| `GeoModelCore/GmcSplit.h:26` | `GmcSplitResult` 含 `std::string errorMessage` → 已改为 `char[256]` | ✅ **已修复** | 固定缓冲区避免 STL 跨边界；`GmcSplit.cpp` `makeError` 改 `const char*` + `snprintf`；字符串拼接用临时缓冲区避免 UB |
| `GeoModelCore/GmcMesh.h:22` | `exportStl` 参数 `const std::string&` → 已改为 `const char*` | ✅ **已修复** | 避免 `std::string` 跨 DLL 边界 |
| `Engine/Common/EngineAPI.h` | Engine2D/3D/Common/Persistence 共享 `ENGINE_EXPORTS` 导致重复导出 | ✅ **已修复** | 已拆分为 `ENGINE_API`/`ENGINE2D_API`/`ENGINE3D_API` 独立宏 |
| `Engine/Common/IUndoRedoCommand.h` | 析构 `= default` 内联 + `std::string description()` 纯虚 → STL 跨边界 | ✅ **已修复** | 析构已移至 `IUndoRedoCommand.cpp`，`description()` 改为 `const char*` |
| `Engine/Common/ISceneManager.h` | 析构 `= default` 内联 + `std::vector<EntityId>` / `std::function` 纯虚 | ✅ **已修复** | 析构已移至 `ISceneManager.cpp`，虚函数改为回调/POD |
| `Engine/Common/TextItem.h` | 析构 `= default` 内联 + `std::string text` 成员 + `TextItemList` 别名 | ✅ **已修复** | 析构已移至 `TextItem.cpp`，`std::string` 改为固定缓冲区 |
| `Engine/Common/SyEntity.h` | 析构 `= default` 内联 + `std::string strName` + `std::vector<Vec2d>` 虚函数 | ✅ **已修复** | 析构已移至 `SyEntity.cpp`，`strName` 改为 private + 访问器方法 |
| `Engine/Common/SyLayer.h` | 析构/移动 `= default` 内联 + `std::string strName` + `getName()` 返回 `const std::string&` → STL 跨边界 | ✅ **已修复** | 析构/移动已移至 `SyLayer.cpp`；`getName()`/`setName()` 改为 `const char*` |
| `Engine/2D/SySmartLine.h` | 析构/移动操作 `= default` 内联 + `releaseSegments()` 返回 `vector<unique_ptr>` | ✅ **已修复** | 析构/移动已移至 `SySmartLine.cpp`，`releaseSegments()` 改为回调模式 |
| `Engine/2D/TextConverter.h` | 析构 `= default` 内联 + `std::string` 成员/返回类型 | ✅ **已修复** | 析构已移至 `TextConverter.cpp` |
| `Engine/2D/UndoRedoManager.h` | 嵌套 `Command` 及子类析构 `= default` 内联 + `std::string`/`std::function` 参数 | ✅ **已修复** | 析构已移至 `UndoRedoManager.cpp` |
| `Engine/2D/IUndoRedoManager.h` | 析构 `= default` 内联 + `std::unique_ptr` / `std::string` 纯虚 | ✅ **已修复** | 析构已移至 `IUndoRedoManager.cpp`，`unique_ptr` 改为 raw pointer，`std::string` 改为 `const char*` |
| `Engine/2D/ILayerManager.h` | 析构 `= default` 内联 + `onLayerAdded` 含 `std::string` 纯虚 | ✅ **已修复** | 析构已移至 `ILayerManager.cpp`，`onLayerAdded` 改为 `const char*` |
| `Engine/Common/SceneRenderContract.h` | `ISceneGeometrySink`/`ISceneDataSource` 析构 `= default` 内联 + `std::vector`/`std::string` 纯虚 | ✅ **已修复** | 析构已移至 `SceneRenderContract.cpp`，虚函数改为 POD 数组/回调/buffer |
| `FileIO/IFileParser.h` | 析构 `= default` 内联 + `std::vector<std::string>` / `std::string` 纯虚 | ✅ **已修复** | 析构已移至 `IFileParser.cpp`，改为回调/buffer/`const char*` |
| `FileIO/IFileWriter.h` | 析构 `= default` 内联 + `std::string` 纯虚 | ✅ **已修复** | 析构已移至 `IFileWriter.cpp`，改为 buffer/`const char*` |
| `FileIO/SyCryptoProvider.h` | 析构 `= default` 内联 + `CryptoResult` 含 `std::string`/`std::vector` + `std::string`/`std::vector` 纯虚 | ✅ **已修复** | 析构已移至 `ISyCryptoProvider.cpp`，`CryptoResult` 改为固定缓冲区+POD 指针，虚函数改为 `const char*`/POD 数组 |
| `UI/Common/IDatabase.h` | 析构 `= default` 内联 + `std::map`/`std::vector`/`std::string` 纯虚 | ✅ **已修复** | 析构已移至 `IDatabase.cpp`；新增 `KvPair` POD 结构体 + `QueryRowCallback` 回调替代 `std::map`/`std::vector`；`std::string` 改为 `const char*`/固定缓冲区 |
| `UI/Common/IBusinessDataRepository.h` | 析构 `= default` 内联 + `std::map`/`std::vector`/`std::string` 纯虚 | ✅ **已修复** | 析构已移至 `IBusinessDataRepository.cpp`；复用 `KvPair`/`QueryRowCallback`；`std::string` 改为 `const char*` |
| `UI/Common/SceneDocumentBase.h` | `std::vector<std::string>` / `std::string` 纯虚 | ✅ **已修复** | 改为回调/buffer/`const char*` |
| `UI/Common/SceneBuilderBase.h` | `std::shared_ptr` 纯虚 + `std::string` 纯虚 | ✅ **已修复** | `createDefaultScene()` 返回 `SceneDocumentBase*` 替代 `shared_ptr` + 新增 `destroyScene()`；`defaultRootName` 已 buffer 模式 |
| `PyBindCore` | ~~直接通过 pybind11 绑定 Engine C++ 类~~ → 实际已通过 PyFacade 门面层隔离 | ✅ **已修复** | PyBindCore 已有 `PyFacade` 值类型隔离（Vec2/EntityRef/EntitySnapshot）；移除未用的 `DocumentFacade::scene()`（泄露 Engine 内部类型）；`FacadeTypes.h`/`DocumentFacade.h`/`SceneGateway.h` 补齐 ABI 注解（同编译器/同 CRT 内部使用） |
| `Nesting/NestingDLL.h` | 进度回调参数含 `const char*` 但未指定编码；版本查询已补齐 | ✅ **已修复** | 已在注释中注明 UTF-8；`Nesting_GetVersion` 已补齐 |
| `Nesting/NestingDLL.h` | ~~句柄为 `void*`~~ → 已改为 `struct NestingJobImpl*` | ✅ **已修复** | 类型安全不透明句柄；`NestingAPI.cpp` 内部 cast 改 `reinterpret_cast`；`Engine2D/NestingEngine.cpp` 调用方 `ProgressBridge::job` 改 `NestingJobHandle` |
| `Vision/VisionDLL.h` | 句柄为 `void*`，非类型安全；版本查询已补齐 | ✅ **已修复** | 建议改为类型安全句柄；`Vision_GetVersion` 已补齐 |
| `Engraving/EngravingCAPI.h` | 句柄为 `void*`，非类型安全 | ✅ **已修复（2026-08-16）** | 已重写为类型安全句柄 `struct EngravingVolumeImpl*`；POD 结构 + `structSize` ABI 校验 + `EngravingResultCode` 错误码 + 日志回调 + thread_local 错误信息，覆盖版本/参数/包围盒/切片/轮廓/加工器句柄/一键生成/内存释放全流程 |
| `Engraving/LaserStrategy.h` | `getName()` 返回 `std::string` + `adjustParams` 含 `const std::string&` 纯虚 | ✅ **已修复** | `getName()` 改为 `const char*`；`adjustParams` 改为 `const char*` |
| `Engraving/EngravingImageAPI.h` | `generate2d5ToolpathFromImageFile` 含 `const std::string&` | ✅ **已修复** | 改为 `const char*` |
| `UI/2D/CMakeLists.txt` | `Src/` 目录为 PUBLIC include | ✅ **已修复** | `Src/` 已改为 PRIVATE |
| `UI/3D/CMakeLists.txt` | `Src/` 目录为 PUBLIC include | ✅ **已修复** | `Src/` 已改为 PRIVATE |
| `FileIO/FileIOManager.h` | 导出类含 `std::unique_ptr`、`std::function`、`std::vector`、`std::string` | ✅ **已修复** | C1 收口（2026-07-31）：回调改 C 函数指针+`void* ctx`；`importFile/exportFile` 改 `const char*`+裸指针数组+`errorBuffer`；新增 `deleteEntities/freeEntityArray`；头文件不再依赖 IFileParser/IFileWriter/STL |
| `FileIO/FileIOExport.h` | ~~C API 入口未包 `try/catch(...)`~~ | ✅ **已删除** | C API 层（FileIOExport.h/.cpp）已整体删除，异常安全问题随之消除 |
| `Render/Common`（旧，已删除） | ~~依赖 Qt 类型~~ → 已移除 `#include <QPointF>` | ✅ **已修复（历史）** | 原 `RenderTypes.h` 中 QPointF 已移除；`Render/Common` 等旧渲染库已整体删除，渲染统一由 `Renderx/SanYiRender` 承担 |
| `UI/Common/LaserToolpathBridge.h` | `executeLaserToolpath` 用 `const std::vector<LaserToolpathPoint>&` 入参 | ✅ **已修复** | 改为 `const LaserToolpathPoint*, int count`（2026-07-31 P0 修复） |

### 8. FileIO.dll 导出特点（已落地）

FileIO 现已通过新增的 C API facade 实现了跨 IDE / 跨编译器兼容。

#### 两套导出宏

| 宏 | 所在文件 | 用途 | 跨编译器 |
|---|---------|------|---------|
| `FILEIO_API` | `FileIO/FileIOAPI.h` | C++ 类/函数导出（`__declspec` / `visibility`） | **否**（C++ name mangling） |
| `_FILEIO_C_API` | `FileIO/FileIOExport.h` | C API 函数导出（`extern "C"` 文件级包裹） | **是**（标准 C ABI） |

#### 平台检测策略

`FileIOAPI.h` 使用三路预定义宏：

```cpp
#if defined(_WIN32) || defined(_WIN64)   → FILEIO_PLATFORM_WINDOWS
#elif defined(__APPLE__)                  → FILEIO_PLATFORM_APPLE
#elif defined(__linux__) || defined(__unix__) → FILEIO_PLATFORM_LINUX
```

- **Windows**：`__declspec(dllexport)` / `__declspec(dllimport)` + `__stdcall`（C API）
- **Linux/macOS**：`__attribute__((visibility("default")))`
- 消除了原实现中 `__APPLE__` 死代码（Apple Clang 同时定义 `__APPLE__` 和 `__clang__`，后者被优先匹配）

#### C API 设计

- **头文件**：`FileIO/FileIOExport.h` — 纯 C 头文件，不依赖任何 C++ 头文件
- **句柄**：`typedef struct FioManagerImpl FioManager` — 不透明类型安全指针（非 `void*`）
- **结果**：`FioResult` — 固定缓冲区模式，`char error_message[512]` 避免 STL 跨边界
- **生命周期**：`fio_manager_create()` / `fio_manager_destroy()` — placement new + `malloc`/`free`，跨 CRT 安全
- **文件操作**：`import` / `export` / `convert` — 通过不透明句柄隐式传递图元数据
- **回调**：C 函数指针 + `void* user_data` 模式
- **内存管理**：扩展名列表返回 `char**`，配套 `fio_free_string_array()` 释放

#### 构建配置

- `FILEIO_EXPORTS` 由 CMake 在构建 DLL 时自动定义（`target_compile_definitions(... PRIVATE FILEIO_EXPORTS)`）
- Windows 版本资源通过 `CMake/VersionResource.rc.in` 模板自动生成
- 依赖：Utiltiy、EngineCommon、Engine2D、protobuf、NanoSVG（均为内部 C++ DLL）

#### 已知限制

| 限制 | 说明 |
|------|------|
| C++ API 跨编译器 | 不兼容。C++ 类导出依赖编译器 name mangling |
| 图元数据访问 | C API 不暴露 `SyEntity` 数据。图元通过句柄隐式传递，仅支持 `import→export` 管道模式 |
| 线程安全 | 继承 `FileIOManager` 的非线程安全约束 |
| 异常安全 | C API 入口未包 `try/catch(...)`，异常可能跨 DLL 边界（待改进） |

### 9. License.dll 导出特点

License（对应 CMake 目标 `License`）通过完整的 C API facade 实现跨编译器兼容，底层复用 C++ `LicenseManager` + OpenSSL。

#### 两套导出宏

| 宏 | 所在文件 | 用途 | 跨编译器 |
|---|---------|------|---------|
| `LICENSE_API` | `License/LicenseAPI.h` | C++ 类/函数导出（`__declspec` / `visibility`） | **否**（C++ name mangling） |
| `LICENSE_C_API` | `License/LicenseAPI.h` | C 链接辅助宏（`extern "C"`） | **是**（标准 C ABI） |

#### 平台检测策略

`LicenseAPI.h` 使用三路预定义宏（同 FileIO）：

```c
#if defined(_WIN32) || defined(_WIN64)   → LICENSE_PLATFORM_WINDOWS
#elif defined(__GNUC__) || defined(__clang__) → LICENSE_PLATFORM_UNIX
#else                                          → LICENSE_PLATFORM_UNKNOWN
```

- **Windows**：`__declspec(dllexport)` / `__declspec(dllimport)`
- **GCC/Clang**：`__attribute__((visibility("default")))`

#### C API 设计

- **头文件**：`License/LicenseDLL.h` — 纯 C 接口，外层 `extern "C"` 包裹 + 每个函数宏双重保护
- **句柄**：`typedef struct LicenseContext LicenseContext` — 不透明类型安全指针
- **结果**：`enum LicenseResult` — 整数错误码（含 `LICENSE_OK = 0`、`LICENSE_ERR_INVALID = -1` 等）
- **版本兼容**：`LicenseConfig` / `LicenseInfo` 结构体含 `structSize` 字段做 ABI 版本控制
- **字符串**：固定大小 `char[128/256]` 缓冲区 + `char* + size_t` 两阶段获取
- **生命周期**：`License_Create()` / `License_Destroy()` — 内部 `unique_ptr<LicenseManager>` + new/delete，跨 CRT 安全建议用 `malloc`/`free`
- **API 数量**：18 个公共函数（检查/激活/机器码/错误查询等）+ 2 个密钥生成 + 2 个测试钩子

#### 构建配置

- `LICENSE_EXPORTS` 由 CMake 在构建 DLL 时自动定义（`target_compile_definitions(... PRIVATE LICENSE_EXPORTS)`）
- 依赖：OpenSSL（`OpenSSL::SSL`、`OpenSSL::Crypto`）、Iphlpapi.lib（Windows）/ IOKit（macOS）
- **无 Qt 依赖**（Qt 仅在消费者端使用）
- Windows 版本资源通过 `version.rc.in` 模板自动生成

#### 防篡改特性

- `LicenseGuard`：6 个相互关联的 `atomic<uint32_t>` 验证变量，通过数学混淆关系连接
- `StrEncrypt`：编译时 XOR 加密敏感字符串（RSA 公钥、错误消息）

#### 已知限制

| 限制 | 说明 |
|------|------|
| C++ API 跨编译器 | 不兼容。C++ 类导出依赖编译器 name mangling |
| 字符串缓冲区固定 | `char[128/256]` 固定大小，超长错误消息会被截断 |
| 全局 `g_checkEnabled` 旁路 | 调试用全局原子变量，设为 0 可完全跳过 License 验证（默认开启） |
| 冗余 `extern "C` 包裹 | 头文件外层 `extern "C" { }` + 每个函数前 `LICENSE_C_API`（宏展开同为 `extern "C"`），无害冗余 |

### 10. CrashHandler.dll 导出特点

CrashHandler 模块（CMake 目标 `CrashHandler`）通过纯 C API facade 提供跨平台异常捕获与 minidump 生成，底层使用 Google Breakpad。

#### 两套导出宏

| 宏 | 所在文件 | 用途 | 跨编译器 |
|---|---------|------|---------|
| `CRASHHANDLER_API` | `CrashHandler/CrashHandlerAPI.h` | 函数导出（`__declspec` / `visibility`） | **否**（C++ name mangling） |
| `CRASHHANDLER_C_API` | `CrashHandler/CrashHandlerAPI.h` | C 链接辅助宏（`extern "C"`） | **是**（标准 C ABI） |

#### 平台检测策略

```c
#if defined(_WIN32) || defined(_WIN64)   → dllexport/dllimport
#elif defined(__GNUC__) || defined(__clang__) → visibility("default")
```

- **Windows**：`__declspec(dllexport)` / `__declspec(dllimport)`
- **GCC/Clang**：`__attribute__((visibility("default")))`
- 不区分 macOS/Linux（统一归入 GCC/Clang 分支）

#### C API 设计

- **头文件**：`CrashHandler/CrashHandlerDLL.h` — 纯 C 接口，外层 `extern "C"` 包裹 + 每个函数宏双重保护
- **句柄风格**：无句柄（全局单例模式）
- **结果**：`enum CrashHandlerResult` — 整数错误码，`CRASHHANDLER_OK = 0`，负数为错误
- **配置**：`CrashHandlerConfig` — POD 结构体，含 `uint32_t structSize` 做 ABI 版本控制
- **API 数量**：13 个公共函数（初始化/关闭/minidump/回调/错误查询/清理）

| 函数 | 说明 |
|------|------|
| `CrashHandler_GetVersion` / `GetVersionString` | 版本查询 |
| `CrashHandler_ConfigInit` | 配置结构体初始化（填充 `structSize`） |
| `CrashHandler_Initialize` / `Shutdown` | 生命周期管理 |
| `CrashHandler_WriteMinidump` | 手动触发 minidump |
| `CrashHandler_SetCrashCallback` | 设置 C 回调（`int (*)(const char*, int, void*)` |
| `CrashHandler_GetLastDumpPath` | 获取最近 dump 路径 |
| `CrashHandler_GetDumpFileCount` / `GetDumpFilePath` | 枚举 dump 文件 |
| `CrashHandler_CleanOldDumps` | 清理过期 dump |
| `CrashHandler_GetLastErrorMessage` | 错误消息查询 |

#### 异常安全

- 每个导出函数体包在 `try/catch(const std::exception&)` / `try/catch(...)` 中
- **不会**有 C++ 异常穿越 DLL 边界

#### 线程安全

- 错误消息使用 `thread_local char g_lastError[512]`，线程安全
- 内部 `std::mutex` 保护 Breakpad 状态

#### 构建配置

- `CRASHHANDLER_EXPORTS` 由 CMake 在构建 DLL 时自动定义（`PRIVATE`）
- 依赖：`unofficial-breakpad`（vcpkg）、C++17 STL
- **无 Qt 依赖**
- `CRASHHANDLER_VERSION` 宏定义为 `0x010000`（v1.0.0）
- Windows 版本资源通过 `sanyi_add_version_info()` 自动生成

#### 已知限制

| 限制 | 说明 |
|------|------|
| C++ API 跨编译器 | 不兼容。C++ 类导出依赖编译器 name mangling |
| 冗余 `extern "C"` 包裹 | 同 License：头文件外层包裹 + 每个函数宏重复声明 `extern "C"` |
| 符号可见性 | CMake 未设置 `-fvisibility=hidden`（依赖根 CMake 全局配置），GCC/Clang 可能有非预期导出符号 |
| 回调签名不一致 | C 回调 `(const char*, int, void*)` → 内部适配为 `std::function<bool(string, bool)>` |

### 11. 编译器与平台兼容建议

当前 CMake 配置已满足以下原则（不需要改）：

```cmake
# CMakeLists.txt:70-72
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# CMakeLists.txt:76        —— 统一 /MD
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
```

需要补充：
- 每个对外 C API 的 DLL 构建后应运行 `dumpbin /exports` 审计导出符号
- Linux/macOS 构建默认隐藏符号，只显式导出 C API 标记的符号
- .def 文件当前未使用，建议对外 facade DLL 增加 `.def` 精确控制

---

---

## 十八、导出宏规范

### 1. 工程现有的导出宏全景

| 导出宏 | 所在头文件 | 构建定义 | 是否有 extern "C" 分离宏 |
|---|---|---|---|
| `UTILITY_API` | `UtilityAPI.h` | `UTILITY_EXPORTS` | 无 |
| `LOG_API` | `LogAPI.h` | `LOG_EXPORTS` | 无 |
| `LICENSE_API` | `LicenseAPI.h` | `LICENSE_EXPORTS` | 有 `LICENSE_C_API`（`LicenseAPI.h`） |
| `CRASHHANDLER_API` | `CrashHandlerAPI.h` | `CRASHHANDLER_EXPORTS` | 有 `CRASHHANDLER_C_API`（`CrashHandlerAPI.h`） |
| `ENGINE_API` | `EngineAPI.h` | `ENGINE_EXPORTS` | 无 |
| `ENGINE2D_API` | `EngineAPI.h` | `ENGINE2D_EXPORTS` | 无 |
| `ENGINE3D_API` | `EngineAPI.h` | `ENGINE3D_EXPORTS` | 无 |
| `RENDER_API` | `render/render.h`（Renderx） | `RENDER_EXPORTS` | 无 |
| `UICOMMON_API` | `UICommonAPI.h` | `UICOMMON_EXPORTS` | 无 |
| `UI2D_API` | `UI2DAPI.h` | `UI2D_EXPORTS` | 无 |
| `UI3D_API` | `UI3DAPI.h` | `UI3D_EXPORTS` | 无 |
| `FILEIO_API` | `FileIOAPI.h` | `FILEIO_EXPORTS` | 有 `FILEIO_C_API`（`FileIOAPI.h`）+ `_FILEIO_C_API`（`FileIOExport.h`） |
| `NESTING_API` | `NestingExport.h` | `NESTING_EXPORTS` | 有 `NESTING_C_API` |
| `GEOMODEL_API` | `GeoModelDLL.h` | `GEOMODEL_EXPORTS` | 无 |
| `ENGRAVING_API` | `EngravingAPI.h` | `ENGRAVING_EXPORTS` | 无 |
| `HARDWARE_API` | `HardwareAPI.h` | `HARDWARE_EXPORTS` | 无 |
| `NETWORK_API` | `NetworkAPI.h` | `NETWORK_EXPORTS` | 无 |
| `VISION_C_API` | `VisionDLL.h` | `VISION_EXPORTS` | 宏内包含 `extern "C"` |
| `PYTHONHOST_API` | `PythonHostAPI.h` | `PYTHONHOST_EXPORTS` | 无 |

### 2. 建议统一模式

对外 C ABI 的 DLL 建议统一为以下模式：

```c
#pragma once

#if defined(_WIN32)
    #if defined(MODULENAME_BUILD)
        #define MODULENAME_API __declspec(dllexport)
    #else
        #define MODULENAME_API __declspec(dllimport)
    #endif
#else
    #if defined(MODULENAME_BUILD)
        #define MODULENAME_API __attribute__((visibility("default")))
    #else
        #define MODULENAME_API
    #endif
#endif
```

**与当前工程做法的差异统一建议：**

| 当前差异 | 建议统一方向 |
|---|---|
| Vision 用 `VISION_C_API` 直接包含 `extern "C"` | 拆分为：`VISION_API`（导出宏）+ 头文件级别 `extern "C"` 包裹 |
| Nesting 用 `NESTING_C_API + NESTING_API` 两个宏分开 | 保持此模式，外部 C 用户只用 `NESTING_C_API` |
| 其他模块无 `extern "C"` 分离宏 | 建议统一增加 `*_C_API` 宏或直接在头文件外层包裹 |
| GeoModelCore 含 `#pragma warning(disable : 4251)` | **禁止**—— 存在即说明有 STL 跨边界 |

### 3. 建议统一错误码

```c
// SanyiResult.h（新建，所有 facade 共用）
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SanyiResult {
    SanyiResult_Ok               = 0,
    SanyiResult_InvalidArgument  = -1,
    SanyiResult_NullPointer      = -2,
    SanyiResult_OutOfRange       = -3,
    SanyiResult_NotFound         = -4,
    SanyiResult_NotSupported     = -5,
    SanyiResult_Busy             = -6,
    SanyiResult_Timeout          = -7,
    SanyiResult_NoMemory         = -8,
    SanyiResult_VersionMismatch  = -9,
    SanyiResult_InternalError    = -99
} SanyiResult;

#ifdef __cplusplus
}
#endif
```

---

---

## 十九、工程构建体系标准

### 1. 当前 CMake 体系关键配置

```cmake
# 根 CMakeLists.txt
project(SanYiCAD VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
```

### 2. 统一 DLL 创建函数

`CMake/Utils.cmake` 中 `sanyi_add_shared_library()` 是工程统一的 DLL 创建入口。

```cmake
# 示例调用
sanyi_add_shared_library(Engine2D
    DESCRIPTION "SanYi CAD 2D Engine"
    EXPORT_MACRO "ENGINE_EXPORTS"
    ...
)
```

**规范要求：**
- 所有新 DLL 必须使用 `sanyi_add_shared_library()`
- EXPORT_MACRO 必须与头文件中 `#ifdef XXX_EXPORTS` 匹配
- 版本号通过 `VERSION_MAJOR/MINOR/PATCH` 参数控制

**统一入口能力（`CMake/Utils.cmake` L203 起）：**

| 参数 | 说明 |
|------|------|
| `SOURCES` / `HEADERS` | 源文件与头文件（推荐绝对路径；相对路径会因内部 `file(RELATIVE_PATH)` 拆分 source_group 而报错） |
| `PUBLIC_INCLUDE_DIRS` / `PRIVATE_INCLUDE_DIRS` | 公开/私有头文件搜索路径 |
| `COMPILE_DEFINITIONS` / `COMPILE_OPTIONS` | 均为 PRIVATE 附加；需要 PUBLIC 的定义请在调用后自行 `target_compile_definitions(PUBLIC ...)` |
| `LINK_LIBRARIES` | 透传给 `target_link_libraries`，可带 `PUBLIC`/`PRIVATE` 关键字 |
| `QT_COMPONENTS` | 通过 `sanyi_find_qt` 查找并 PUBLIC 链接 `Qt6::*` |
| `FOLDER` / `DESCRIPTION` / `EXPORT_MACRO` / `VERSION_MAJOR/MINOR/PATCH` | 工程分组、版本资源、导出宏 |
| `AUTOMOC` / `AUTOUIC` / `AUTORCC` / `UNITY_BUILD` | Qt / 统一构建开关 |

**内部自动完成：** MSVC `/utf-8 /FS`（非 MSVC `-Wall -Wextra -Wpedantic`）、`cxx_std_17`、`DEBUG_POSTFIX _d`、`VERSION/SOVERSION`、`source_group`（外部文件自动归入 `External` 组，规避 source_group ROOT 前缀检查）、`sanyi_add_version_info`（version.rc）、`sanyi_add_debug_symbols`（PDB/dSYM/.debug → `symbols/`）。

**局限（需在调用后补充）：**
- 不支持 PUBLIC compile definitions
- 不支持 `POST_BUILD` / `set_source_files_properties` / 自定义 source_group / translations 自定义 target
- 不处理基于 `install(EXPORT)` 的包导出——使用 `$<BUILD_INTERFACE:...>` / `$<INSTALL_INTERFACE:...>` 时可在调用后自设 `target_include_directories`

**全量迁移状态（2026-08-11）：** 全部 19 个 DLL 模块已迁移到统一入口：Utility、Log、License、CrashHandler、EngineCommon、Engine2D、Engine3D、SanYiRender（Renderx）、GeoModelCore、Engraving、UICommon、UI2D、UI3D、FileIO、Nesting、Hardware、Network、Vision、PythonHost。已通过完整工程 configure + Release 构建验证（18 个 DLL 生成，Network 因 `BUILD_NETWORK=OFF` 顶层跳过）。迁移中修复统一入口缺陷：`version.rc` 需要 `PROJECT_VERSION_MAJOR/MINOR/PATCH` 分量变量（此前未设置导致 `FILEVERSION ,,,0` / RC2127）。

**迁移注意事项（登记，供后续维护）：**
- Renderx：源文件列表需经绝对路径归一化（其原生为相对路径写法）
- GeoModelCore：PUBLIC include 必须使用 `$<BUILD_INTERFACE:>` / `$<INSTALL_INTERFACE:include>`，否则 `install(EXPORT)` 报 INTERFACE_INCLUDE_DIRECTORIES 含 source 前缀路径
- `SANYI_LICENSE_SYMBOLS` / `SANYI_CRASHHANDLER_SYMBOLS` 已由统一入口无条件接管（默认 ON，option 现仅兼容保留）

### 3. DLL 版本资源

Windows `.rc` 版本信息通过 CMake 配置自动生成：

```cmake
# CMake/version.rc.in → ${target}_version.rc
# 在 sanyi_add_shared_library() 内部自动调用
sanyi_add_version_info(${target} "${target}" "${description}")
```

### 4. 调试符号分离

`CMake/Utils.cmake` 中的 `sanyi_add_debug_symbols()` 实现 PDB 分离：

- Windows: PDB → `symbols/` 目录
- macOS: dSYM → `symbols/` 目录
- Linux: `.debug` → `symbols/` 目录

### 5. 产物输出目录

```text
${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}/
    Debug/
        *.exe, *.dll, *.pdb, *.pyd
    Release/
        *.exe, *.dll, *.pdb, *.pyd
${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}/
    Debug/
        *.lib
    Release/
        *.lib
```

---

---

## 二十、插件系统标准

### 1. 当前插件系统

`UI/Plugin/CommandPluginExport.h` 定义：

```cpp
#define SANYI_COMMAND_PLUGIN_EXPORT extern "C" __declspec(dllexport)

#define SANYI_DEFINE_COMMAND_PLUGIN(PluginClass)                          \
    SANYI_COMMAND_PLUGIN_EXPORT ICommandPlugin* createCommandPlugin()     \
    {                                                                     \
        return new PluginClass();                                         \
    }                                                                     \
    SANYI_COMMAND_PLUGIN_EXPORT void destroyCommandPlugin(ICommandPlugin* plugin) \
    {                                                                     \
        delete plugin;                                                    \
    }
```

### 2. 插件加载机制

`UI/Common/Src/Plugin/CommandPluginLoader.cpp` 通过 `QLibrary` 动态加载。

### 3. 插件规范

- 必须导出 `extern "C" ICommandPlugin* createCommandPlugin()`
- 可选择导出 `extern "C" void destroyCommandPlugin(ICommandPlugin*)`
- 生命周期：DLL 内 `new`，DLL 内 `delete`
- 插件通过 `ICommandPlugin` 纯虚接口交互
- 主程序通过 `commandPluginRegistryLoadFromDirectory()` 批量扫描目录

### 4. 插件开发模板

```cpp
#include <UI/Plugin/CommandPluginExport.h>

class MyPlugin final : public ICommandPlugin {
public:
    QString name() const override { return "MyPlugin"; }
    // ... 实现其他虚函数
};

SANYI_DEFINE_COMMAND_PLUGIN(MyPlugin)
```

---

---

## 二十一、标准接口范式

### 1. 生命周期范式（工厂 + 销毁）

### 2. 错误码范式

```c
// Nesting 范例
NESTING_C_API NESTING_API int Nesting_Run(NestingJobHandle job);

// 返回 NESTING_OK=0 成功，负数为错误码
```

### 3. 字符串输出范式

```c
CADSDK_API CAD_Result CAD_GetLastErrorMessage(char* buffer, int buffer_size);
CADSDK_API int CAD_GetLastErrorMessageLength(void);  // 两阶段调用
```

### 4. 进度回调范式

```c
// Nesting 范例
typedef void (*NestingProgressCallback)(
    double overallPercent,
    const char* stageName,
    const char* message,
    int elapsedMs,
    void* userData);

NESTING_C_API NESTING_API void Nesting_SetProgressCallback(
    NestingJobHandle job,
    NestingProgressCallback callback,
    void* userData,
    int useThreadPool);
```

### 5. 版本查询范式

```c
// Engine 已有
extern "C" ENGINE_API int SanYiEngineVersion();
```

---

---

## 二十二、各平台构建与发布标准

### Windows

- CMake 配置 `/MD` 统一运行时（已全局配置）
- `.rc` 版本信息自动生成（已通过 `sanyi_add_version_info()` 实现）
- PDB 分离（已通过 `sanyi_add_debug_symbols()` 实现）
- 通过 `windeployqt` 自动部署 Qt DLL（`sanyi_deploy_qt_dlls()`）

### Linux

- `-fvisibility=hidden` 统一符号隐藏（已全局配置）
- 通过 `objcopy --only-keep-debug` 分离调试符号（已实现）
- 注意 `SONAME` 和 `rpath` 设置
- 构建后运行 `ldd` 检查依赖

### macOS

- `install_name` 需与发布路径一致
- 通过 `dsymutil` 分离符号（已实现）
- 发布前代码签名

---

---

## 二十三、发布前审核清单

### 1. 接口层
- [ ] 是否新增了稳定导出？
- [ ] 是否修改了旧导出签名？
- [ ] 是否新增了不稳定类型（STL/Qt/模板）？
- [ ] 是否有接口没有明确生命周期？

### 2. 类型层
- [ ] 是否暴露了 STL？
- [ ] 是否暴露了 Qt？
- [ ] 是否暴露了模板实例？
- [ ] 是否暴露了 C++ 异常语义？
- [ ] 结构体是否带 `static_assert` 校验大小？

### 3. 内存层
- [ ] 是否存在跨 DLL new/delete？
- [ ] 是否存在不明确释放责任的返回值？
- [ ] 是否存在数组/字符串未定义释放规则？

### 4. 版本层
- [ ] 是否更新版本号？
- [ ] 是否更新 ABI 主版本？
- [ ] 是否保留旧接口？
- [ ] 是否更新兼容说明？

### 5. 平台层
- [ ] Windows/Linux/macOS 是否都检查过？
- [ ] 是否有平台专有路径写死？
- [ ] 是否有平台专有类型泄漏？
- [ ] 是否有多平台构建验证？

### 6. 文档层
- [ ] 是否同步更新了 DLL 标准文档？
- [ ] 是否补充了示例代码？
- [ ] 是否补充了错误码和版本规则？

---

---

## 二十四、最终原则

1. **内部模块不为了对外 ABI 牺牲重构自由。**
2. **对外边界必须稳定、克制、版本化。**
3. **任何跨 DLL 行为都必须有明确生命周期和所有权。**
4. **所有导出必须经过文档确认，不允许临时暴露。**
5. **公共头文件必须能支撑多编译器、多平台构建。**
6. **一旦导出，就视作长期承诺。**
7. **不要添加 `add_compile_options(/wd4251)`** —— 存在 4251 警告说明有 STL 跨边界，应修复而非压制。
8. **不要添加 `#pragma warning(disable : 4251)`** —— 同上，应从根源修复（析构函数分离或裸指针 PIMPL）。
9. **所有新增文件的接口头文件必须添加注释说明其 ABI 边界属性。**
10. **导出类的析构函数必须声明在头文件、定义在 .cpp** —— 避免 STL 成员内联销毁跨 DLL 边界。
11. **`std::unique_ptr<Impl>` PIMPL 在导出类中应改为裸指针 `Impl*`** —— 避免 C4251 警告。

---

---

## 二十五、编译器与符号可见性规范

### 1. 现状（已验证）

| 项目 | 现状 | 要求 |
|------|------|------|
| 根 CMake 符号隐藏 | `CMAKE_CXX_VISIBILITY_PRESET hidden` + `CMAKE_VISIBILITY_INLINES_HIDDEN ON`（根 `CMakeLists.txt`） | ✅ 保持 |
| 统一运行时 | `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"`（统一 `/MD`） | ✅ 保持，禁止混用 `/MT` |
| 导出宏 | 各模块统一 `XXX_API`（见「十八、导出宏规范」） | ✅ 保持 |
| 平台分支 | `FileIOAPI.h` 曾存在 `__APPLE__` 死代码（Apple Clang 同时定义 `__APPLE__`/`__clang__`，先匹配 `__clang__`） | ✅ 已修复：三路检测顺序修正 |
| `.def` 文件 | 当前未使用 | ⚠️ 建议对外 facade DLL 增加 `.def` 精确控制导出 |
| 导出符号审计 | 构建后未强制运行 `dumpbin /exports` | ⚠️ 建议纳入发布流程 |

### 2. 规则

1. Windows 用 `__declspec(dllexport)`/`dllimport`；Linux/macOS 用 `-fvisibility=hidden` + `__attribute__((visibility("default")))`。
2. 导出符号最小化——只导出必要符号。
3. `inline` 函数不导出，STL 便捷包装应定义为 header 内联（编译进调用方，不跨 DLL）。

---

## 二十六、内存分配/释放风险模式

### 1. 核心原则

**谁分配谁释放，且分配与释放在同一 DLL / 同一 CRT 内。**

### 2. 风险场景

| 风险场景 | 说明 | 处理 |
|----------|------|------|
| 客户端 `delete` DLL 对象 | 导出类对象由 DLL `new`，客户端 `delete` 时跨 CRT | 工厂 + `destroy()`（DLL 内 `delete this`），或 `SyEntityOwnPtr`（自定义 deleter） |
| `std::function` 回调跨 DLL | 回调含调用方堆内存，DLL 内触发可能 ABI 崩溃 | 改 C 函数指针 + `void* ctx` |
| `std::shared_ptr` 跨 DLL | control block 由 DLL 分配，客户端释放时跨 CRT | 改句柄 / `void*` + 对称 release |
| 数组分配不一致 | DLL 返回数组，客户端 `free()` | 配套 `FreeArray(void*)` |

### 3. 已应用

- `SyEntity::destroy()`（`delete this`）+ `SyEntityOwnPtr`/`SyEntityDeleter`
- FileIO `freeEntityArray` / `deleteEntities`
- GeoModelCore `TopoShape` PIMPL
- CrashHandler / License C API 生命周期函数

---

## 二十七、虚函数与 RTTI 规范

| 场景 | 风险 | 处理 |
|------|------|------|
| 纯虚接口跨 DLL | vtable 布局依赖编译器，但纯虚接口配合工厂函数较安全 | ✅ 可接受（`IFileParser`/`IEntity` 等） |
| 虚继承跨 DLL | 布局编译器相关，风险极高 | ❌ 禁止 |
| `dynamic_cast` 跨 DLL | 依赖 RTTI 实现，可能失败 | ❌ 禁用跨 DLL `dynamic_cast`；用虚函数或工厂 |
| 导出类虚析构 | 析构必须由 DLL 定义（非内联） | ✅ 析构分离至 `.cpp`（模式 5） |

**当前状态**：
- `FileIO` 纯虚接口 + 工厂：✅ 安全。
- `Vision` 虚继承（`Shape→Line/Circle/...`）跨 DLL：⚠️ 已标注为沙箱内部模块，`dynamic_cast` 不承诺跨边界。
- `IEntity` 纯虚接口 + `destroy()`：✅ 安全。

---

## 二十八、模板类与内联函数规范

| 项目 | 现状 | 要求 |
|------|------|------|
| 模板导出 | 无显式 `template class __declspec(dllexport)` | ❌ 禁止导出模板 |
| 模板显式实例化 | `Ut::Vec<T>`/`Ut::Mat<T>` 由 `UTILITY_EXTERN_TEMPLATES` 控制 | ✅ 保持 |
| 内联函数 | 导出类无内联成员定义到 public 头（TopoShape 旧 API alias 为简单转发，风险低） | ⚠️ 内联函数编译进调用方，不跨 DLL；若含 STL 需标注 |
| 模板注册 | `ToolManager3D::registerTool` 模板下沉为非模板 `FactoryFn` | ✅ 已收口 |

**规则**：
1. 模板类/函数不加 `__declspec(dllexport)`。
2. 模板注册/工厂函数逻辑下沉到 `.cpp`，对外非模板。
3. STL 便捷包装可作 header 内联（编译进调用方，不跨 DLL）。

---

## 二十九、静态成员与全局状态规范

**风险**：静态局部变量单例（如 `CrashHandler::instance()`、`EntityIdGenerator::instance()`）被多个 DLL 分别加载时，每个 DLL 可能有一份拷贝。

| 场景 | 说明 | 处理 |
|------|------|------|
| 进程级单例 | Windows 同进程内多个 DLL 引用同一 CRT 时通常正确 | ⚠️ 跨平台需用导出函数返回单例指针 |
| 跨 DLL 全局状态 | 全局对象依赖跨 DLL 顺序 | 用显式 `Initialize()`/`Shutdown()`（规则 28） |
| `thread_local` | DLL 卸载时不调用析构（Windows 已知问题） | 慎用（规则 27） |

**规则**：单例/缓存/日志等全局状态用 `std::mutex`/`std::atomic` 保护（规则 25）；避免跨 DLL 的全局变量依赖（规则 28）。

---

## 三十、Include 目录组织规范

### 1. 推荐标准（与 OpenCV / Qt / Boost 一致）

```text
ModuleName/
├── include/ModuleName/        # 公共头文件（分发给第三方）
│   ├── ModuleNameAPI.h        # 导出宏
│   ├── ClassA.h
│   └── ClassB.h
├── private/                   # PIMPL 实现头文件（不公开）
│   └── ClassAImpl.h
├── src/                       # .cpp 源文件
│   └── ClassA.cpp
└── CMakeLists.txt
```

### 2. 当前工程状态

| 模块 | 目录 | 状态 |
|------|------|------|
| `GeoModelCore` | `Include/GeoModelCore/` | ✅ 好 |
| `Vision` | `Include/Vision/` | ✅ 好 |
| `Engraving` | `Include/Engraving/` | ✅ 好 |
| `Renderx` | `include/render/` | ✅ 好（小写） |
| `CrashHandler` | `Include/CrashHandler/` | ✅ 好 |
| `Main/Src/RenderCore/` | 头文件混在 `Src/` | ❌ 需抽到 `Main/Include/UiRenderCore/` |

### 3. 规则

1. 公共头文件放 `include/ModuleName/`，使用者 `#include "ModuleName/ClassA.h"`。
2. 私有实现头（PIMPL）放 `private/` 或 `Src/`，不公开。
3. `Src/` 目录在 CMake 中设为 `PRIVATE` include（UI2D/UI3D 已修复）。

---

## 第二部分：现有 DLL 问题与收口现状

> 本节记录工程各模块的 ABI 违规清单、收口批次记录与当前状态。改造现有代码时以此为现状基线。

---

## 全局状态

**日期：2026-08-02**

所有可执行 ABI 修复批次已清零（A/B/C/D/E/F/G/H + PyBindCore + Nesting + Render/Common Qt）。2026-08-13~14 完成活跃违规收口 4 组（readImageInfo + FontUtil/FontManager + CommandPluginRegistry + SceneUndoCommands）以及 `SceneManager::addEntities` C-safe 补齐；详见「批次 H」及 §3.3/§3.4/§3.7 表格。
析构分离审计已完成两轮（§7.4 + §7.6），共 28 个导出类/接口的内联 `= default` 析构已全部下沉至 `.cpp`。

唯一保留项为 Hardware 的 Qt 类型跨 DLL 边界，属架构决策范畴（见 §3.5、§4 全局结论、§5 ADR）。

最终状态表见 §6，代码级复核清单见 §7。

---

---

## 十三、2026-07-30 STL / ABI 跨 DLL 审计结论

> 本节不是“理想设计”，而是对当前工程公开头文件的审计结论。结论分为三类：
> 
> - **已属事实**：公开头文件中确实存在 STL 出现在跨 DLL public 接口的情形
> - **可接受但需标注**：仅限内部 C++ DLL、同编译器同 CRT 使用，不承诺跨编译器 ABI
> - **必须修复**：纯虚接口、导出函数、对外 facade 中出现 STL，必须收口

### 1. 审计结论是否属实

结论总体**属实**，但需要做两点修正：

1. **“是否违规”要按 ABI 边界判断，而不是按“头文件里有没有 STL”一刀切。**
   - 对于 **公开 ABI 边界**（纯虚接口、`XXX_API` 导出函数、对外 C API facade），`std::string` / `std::vector` / `std::map` 出现即应视为高风险，必须修复或改写。
   - 对于 **内部 C++ DLL**（仅内部同编译器使用），头文件中存在 STL 不一定是 bug，但必须明确“不承诺跨 DLL / 跨编译器 ABI”。

2. **审计报告里的“完全合规”更准确地说是“未发现 STL 直出到 public 接口”。**
   - 这类 DLL 仍需继续维持当前约束，不代表可以放松标准。

### 2. 目前工程中需要区分的三层风险

| 层级 | 说明 | 处理方式 |
|---|---|---|
| P0 | 纯虚接口、导出函数、对外 C API facade 中直接暴露 STL | 立即修复，改为 `const char*`、POD、回调迭代器或 `BinaryBlob` |
| P1 | public 类/结构体/成员函数对外暴露 STL，但该 DLL 仍定位为内部 C++ DLL | 保留实现自由，但必须写明“仅内部同编译器可用” |
| P2 | 仅内部算法、模板、inline、实现细节里使用 STL | 可接受，不作为 ABI 问题 |

### 3. 审计报告中列出的模块，建议按以下方式理解

#### 3.1 必须修复的模块

这些模块的 public 接口已经进入 ABI 边界，**不适合继续直接暴露 STL**：

- `EngineCommon`
- `Engine2D`
- `FileIO`
- `GeoModelCore`
- `UICommon`
- `Engraving`
- `UI3D`
- `UI2D`
- `Vision`

其中最优先的是：

- **纯虚接口**：`ISceneManager`、`IEntity`、`IUndoRedoCommand`、`SceneRenderContract`
- **导出函数**：FileIO / GeoModelCore / UI3D 中的 `XXX_API` 函数
- **对外 facade**：所有已规划为对外 DLL API 的入口

#### 3.2 允许保留但要明确边界的模块

这些模块更适合继续作为内部 C++ DLL 维护，**不建议强行为“零 STL”付出过大重构成本**：

- `Engine3D`
- `Hardware`
- `PythonHost`
- `Utility`
- `Log`

它们可以继续使用 STL，但前提是：

- 不把 STL 直接放进跨 DLL 的稳定 C ABI
- 不把实现细节误写成对外承诺
- 不让下游把它们当成长期冻结接口使用

### 4. 审计报告中的修复模式，建议改成“四选一”

#### 模式 A：`const char*` + 长度

适合：字符串参数、字符串返回值、错误信息

- `std::string` 入参 → `const char*`
- `std::string` 出参 → `char* buffer, size_t size`

#### 模式 B：回调遍历

适合：`std::vector<T>` 返回值

- `return std::vector<T>` → `forEachXxx(callback, ctx)`
- 调用方自己收集数据

#### 模式 C：`BinaryBlob`

适合：复杂数组、批量数据、二进制序列化数据

- `struct BinaryBlob { const uint8_t* data; size_t size; }`
- 由一端序列化，另一端反序列化

#### 模式 D：PIMPL / 裸指针 PIMPL

适合：导出类含 STL 成员

- 头文件只保留 `Impl*`
- 析构 / 拷贝 / 移动在 `.cpp` 中定义
- 避免在头文件中内联析构 STL 成员

### 5. 结合本工程的结论

#### 已经明确修复或已采取保护的点

- `Log.dll` 中 `SyLogConfig` 的 `std::string` 问题已改为 `const char*` + 内部实现结构分离。✅
- `Log.dll` 中 `SyLogger` 的 PIMPL 已改为裸指针 `Impl*`。✅
- `GeoModelCore` 中 `GmcStatus` 的 `std::string` 已改为固定缓冲区 `char message[256]`。✅
- `GeoModelCore` 的 `#pragma warning(disable : 4251)` 已移除。✅
- `GeoModelCore` 中 `GmcBooleanResult`/`GmcSplitResult` 的 `std::string errorMessage` 已改为 `char[256]` 固定缓冲区。✅
- `GeoModelCore` 的 `GmcMesh::exportStl` 参数已从 `const std::string&` 改为 `const char*`。✅
- `GeoModelCore` 的 `GmcMesh`/`GmcIntersection`/`GmcQuery`/`GmcProjection`/`GmcSurface` 已补齐"内部 C++ ABI 层"注解。✅

#### 代码对照审计结论（2026-07-30）

> **2026-07-30 P1 修复轮次更新**：以下问题已全部修复。Engine 导出宏已通过独立文件（EngineAPI.h/Engine2DAPI.h/Engine3DAPI.h）完成拆分；所有析构函数已移至 .cpp；TopoShape PIMPL 的 unique_ptr 因析构已分离而安全；UI2D/UI3D Src/ 已改为 PRIVATE。

- `Engine` 导出宏 **已拆分**：通过 `EngineAPI.h`（ENGINE_API）、`Engine2DAPI.h`（ENGINE2D_API）、`Engine3DAPI.h`（ENGINE3D_API）三个独立文件完成拆分。
- 所有析构函数 **已移至 .cpp**：`IUndoRedoCommand`、`ISceneManager`、`TextItem`、`SyEntity`、`SyLayer`、`SySmartLine`、`TextConverter`、`UndoRedoManager::Command` 的析构均已在 .cpp 中定义。
- `TopoShape` PIMPL **安全**：析构已分离至 .cpp，`unique_ptr<TopoShapeImpl>` 在完整类型可见处析构，无跨 DLL 泄漏风险。
- `UI2D`/`UI3D` 的 `Src/` 目录 **已改为 PRIVATE**：不再暴露给下游消费者。

#### 仍需继续处理或持续关注的点

- `UI2D`、`UI3D` 仍有大量内部头文件含 `std::vector`/`std::string`/`std::unique_ptr`，但已明确标注为内部实现面；公开 ABI 接口已完成主要收口。
- `GeoModelCore` 的 `makeFillet`/`makeChamfer` 已改为 `const double*, size_t`；`getAllFaces`/`getAllEdges`/`getAllVertices` 已改为 `forEachFace/forEachEdge/forEachVertex` 回调模式。`GmcBooleanResult`/`GmcSplitResult` 的 `errorMessage` 已改为 `char[256]` 固定缓冲区。`GmcMesh::exportStl` 已改为 `const char*`。`thicken`/`GmcMeshData`/`GmcBoolean::sectionEdges` 等仍有 `std::vector` 但已标注为内部 C++ ABI 层。
- `Engraving` 的 `Toolpath`/`ToolpathList` 类型别名已标注为内部算法层类型，不作为跨 DLL 稳定 ABI 承诺。
- `Vision` 头文件中大量使用 OpenCV 与 STL，已在 `VisionAPI.h` 显式标注为沙箱型内部模块，不承诺跨 DLL ABI。
- `Engine3D` 的 public 类已标注为内部 C++ DLL，禁止当作外部稳定 ABI。
- `PyBindCore` 已通过 PyFacade 门面层隔离 Engine 类型；`DocumentFacade::scene()` 已移除；ABI 注解已补齐。
- `HardwareLaserService.h`（#16）已标注为 UI2D 内部硬件适配层，不跨 DLL ABI 承诺。
- `SceneDocumentIO3D.h` 导出函数 `cloneAllEntities`/`replaceScene` 带 `std::vector<std::unique_ptr<>>`（UI3D 内部 DLL，按当前标准可接受）。
- `SceneUndoCommands.h` ENGINE2D_API 导出函数原致 `std::vector<std::unique_ptr<>>` → ✅ **已修复**：命令类去除 `ENGINE2D_API` 导出，改为 DLL 内工厂 `createAddEntitiesCommand`/`createDeleteEntitiesCommand`/`createEntitySnapshotsCommand` + C-safe `captureEntitySnapshots`；详情见 §3.4 表格。

---

### 更新 2026-08-13：活跃跨 DLL STL 违规收口进展

以下 4 组活跃违规已全部收口（详见 §3.3/§3.4/§3.7 表格 "✅ 已修复" 标记）：

| 违规组 | 原暴露 STL | 修复方案 | 验证 |
|---|---|---|---|
| `readImageInfo` | `const string&` 路径参数 | `const char* strUtf8Path` | 链接+构建 OK |
| `FontManager/FontUtil` | `string`/`shared_ptr`/`vector<FT_Byte>`/`std::function` | header-inline + `FT_Byte*`+`size` + `const char*` | 链接+构建 OK |
| `CommandPluginRegistry` | `shared_ptr`/`QList<shared_ptr>` | `(ICommandPlugin*, void(*)(ICommandPlugin*))` 工厂 | 链接+构建 OK |
| `SceneUndoCommands` | `vector<unique_ptr<SyEntity>>` 导出 ctor/capture | 命令类去导出 + 3 工厂 + C-safe capture | 链接+构建 OK，UndoRedoRegressionTests 28/28 全通 |

#### 额外收发现：`SceneManager.addEntities` 跨 DLL STL
- `SceneManager.h` 导出类成员 `addEntities(const VecSyEntityPtr&)` (`std::vector<SyEntity*>`) 被 `UI/Common/NestingEngine.cpp` 跨 DLL 调用。
- 补齐 C-safe 重载 `addEntities(SyEntity* const*, size_t)`，NestingEngine 调用改为 `.data()/.size()`，构建+测试通过。

### 6. 对“要不要这样做”的结论

**要做，但要分层做。**

- **必须做**：所有对外 ABI、纯虚接口、导出函数中的 STL 收口。
- **不必强做**：内部 C++ DLL 全面零 STL。这样会显著增加重构成本，且未必符合模块定位。
- **应该做**：把“是否允许 STL”从“按模块名字判断”改成“按 ABI 边界判断”。

换句话说，真正的目标不是“整个工程完全没有 `std::vector` / `std::string`”，而是：

1. **跨 DLL 的稳定边界上没有 STL。**
2. **内部实现可以保留 STL，但必须明确只供内部同编译器使用。**
3. **对外接口统一为 C ABI / POD / 回调 / Blob。**

---

---

## 三十一、跨 DLL STL 违规清单（全模块详细清单）

> **来源**：原 `DLL-ABI复查报告-跨DLL-STL违规清单-2026-07-31.md`（2026-07-31 并入本文档，原文件已删除）。
> 判定原则：
> 1. 跨 DLL 的 public 接口（被 `_API` 导出宏修饰的类/函数/纯虚接口/信号/回调）禁止出现 `std::vector / string / map / unordered_map / shared_ptr / unique_ptr / function / optional / variant` 等 STL 类型。
> 2. STL 容器只能在 DLL 内部使用（通过 PIMPL 隐藏）。
> 3. 跨 DLL 边界只传递 POD 类型；如必须传容器数据，用 `const T* + count` 或 BinaryBlob。
> 4. PIMPL 方案彻底消除 STL 容器跨 DLL 的风险。

已确认全部相关模块均为 SHARED DLL：FileIO、Engine2D/Engine3D、UICommon/UI2D/UI3D、Engraving、GeoModelCore、Vision、Network 等。

### 1. 汇总统计

| 模块 | 违规点 | 严重度 | 被跨 DLL 消费 |
|------|--------|--------|--------------|
| FileIO | ~66 处 | 高 | ✅ Main（12 处 include） |
| GeoModelCore | ~74 处 | 高 | ✅ UI3D |
| Engraving | ~41 处 | 高 | ✅ UI3D |
| UI/Common | ~40 处 | 高 | ✅ UI2D/UI3D/Main |
| Engine/2D | ~50 处 | 高 | ✅ UI2D |
| Engine/3D | ~17 处 | 高 | ✅ UI3D |
| Engine/Common | ~14 处 | 高 | ✅ 全员 |
| UI/3D | ~20 处 | 高 | ✅ Main |
| UI/2D | ~10 处 | 高 | ✅ Main |
| Log | 10 处 | 中 | ✅ 7+ 模块 |
| Hardware / Network | 少量 | 中 | Qt 类型另有隐患 |
| Vision | ~100 处 | 沙箱 | 标注即可 |
| **合计** | **约 350+** | | |

合规（✅ 已收口）：License、Nesting、CrashHandler、Utility、PyBindCore、FileIO 的 `FileImporter`+`FioParseResult`、各模块 C 版本号函数。

### 2. 最严重 Top5

1. `UI/Common/Include/UI/Persistence/SettingsTable.h:13` — `using UiSettingValue = std::variant<int,float,double,bool,std::string>` + `std::map` 私有成员 + 全 public 接口 string 参数。Settings 全链路 DTO，跨 UICommon DLL。✅ 已收口（PIMPL + const char* + forEach）。
2. `UI/Common/Include/Render/RenderTypes.h` — 约 20 处 `std::vector` 结构体成员，经 `UI/2D/RenderWidget.h`（`setSceneCommands`/`setSelectionOutlines`）实际透传跨 DLL。
3. FileIO 全家 — `FileIOManager` 已收口（C1，见 3.7）；`SyDocument` 导出结构体成员仍是 vector/string/map（待 C3）。
4. GeoModelCore 全家 — `TopoShape` 的 boolean/clone/getAllFaces 等返回 `std::shared_ptr<TopoShape>`、`std::vector<shared_ptr>`。
5. ~~Engine/2D 图元族 — 所有 `SyLine/SyCircle/...::clone()` 返回 `std::unique_ptr<SyEntity>`，`SyLine::vPoints` 等 public `vector<Vec2d>` 成员。~~ ✅ **已修复（批次 D + 补全）**：clone() 已返回裸指针 `SyEntity*`；STL 成员已私有化（含 `SyImage::vPixelData`）；`SySmartLine` 的 `addSegment`/`releaseSegments` 已改裸指针/裸指针数组；新增 destroy() + SyEntityOwnPtr 跨 DLL 安全释放。

### 3.5 Hardware / Network / Qt 类型边界待决策

> 这一项不再属于“ABI 违规修复”，而属于**架构边界决策**：是否允许 Qt 类型作为 DLL 边界契约。
>
> 当前结论：`Hardware` 模块中的 `QString` / `QStringList` / `QList` 跨 DLL 使用已经在代码层完成必要收口和注释标注，但是否继续允许 Qt 类型跨边界，需要从平台一致性、插件兼容性、调用方复杂度和长期维护成本四个维度统一决策。
>
> **暂定结论**：在未形成统一架构决策前，保留现状，并在相关 public 头文件中明确标注“Qt 类型仅限内部同编译器/同构建体系使用，不作为跨编译器稳定 ABI 承诺”。

### 3. 分模块违规清单

#### 3.1 Engine/Common（ENGINE_API）

| 文件:行号 | STL 类型 | 位置 | 风险 | 改法 |
|---|---|---|---|---|
| `Engine/Text/Utf8Text.h` | ~~`std::string`~~ | ~~函数参数~~ | ~~高~~ | ✅ **已修复** (`批次 H`)：`getNextCodePoint` 改为 `(const char* strText, size_t textLen, size_t pos, uint32_t& nOutCode)`；`decodeUtf8ToCodePoint`/`encodeCodePointToUtf8` 亦为 `const char*`/`char*` 裸指针接口 |
| `Engine/Text/FontUtil.h` | ~~16,17,19-30,32~~ | ~~`string`/`vector<FT_Byte>`~~ | ~~返回/参数/成员~~ | ~~高/中~~ | ✅ **已修复**：`toUtf8`/`fromUtf8` 作为 header-inline 便捷函数（编译进调用方，不跨 DLL）；`FontData.vData` 由 `vector<FT_Byte>` 改为 `FT_Byte* pBuffer`+`FT_Long nBufferSize`（裸指针+大小，避免 STL 跨 DLL）；`loadFontFace` 入参改为 `const char*`；调用方统一加 `.c_str()` |
| `Engine/Text/FontManager.h` | ~~20,30,12-17~~ | ~~`string`/`const string&`~~ | ~~参数/返回/成员~~ | ~~高/中~~ | ✅ **已修复**：`FontUtil` 命名空间的 toLowerAscii/fontFamilyNamesMatch/styleNameMatchesRequest/compactFamilyKey 移为 header-inline；`loadFontFace` 入参改为 `const char*`；补齐 `<algorithm>`/`<cctype>`；调用方统一加 `.c_str()` |
| `Engine/Text/FontParameters.h:45,51-72` | `vector<TextPathSegment>`/`string`×4 | typedef/成员 | 中 | 数组+计数；`char[256]` |
| `Engine/TextItem.h:68,82,90` | `vector<...>` typedef | typedef | 中 | `const T*+count` |
| `Engine/Layer/SyLayer.h:19,~30` | `string` 构造参数 / `VecSyEntityPtr` 返回 | 构造/返回 | 高 | `const char*`；EntityId 数组+回调 |
| `Engine/EntityIdUtils.h:9` | `std::optional<EntityId>` + `const string&`（**inline**） | inline 函数 | 高 | 拆两个 POD 函数（`bool parse(...,EntityId& out)`） |
| `Engine/Parallel/EngineParallel.h` | `std::future<T>`+`std::function` | 模板方法 | 高 | `void* ctx`+回调 或句柄 |
| `Engine/SyEntity/SyEntity.h` | ~~`virtual std::unique_ptr<SyEntity> clone()=0`~~ | 纯虚返回 | ✅ **已修复** | clone() 已返回裸指针 `SyEntity*`；新增 `destroy()` 虚方法 + `SyEntityOwnPtr`（带 `SyEntityDeleter`）确保跨 DLL 安全释放 |

#### 3.2 Engine/2D（ENGINE2D_API）

> **图元族（SyEntity 目录）public STL 成员收口状态**（批次 D，2026-08-01 更新）：
> - ✅ `SyText`：`strText`/`strFontName` → private，`text()/setText()/fontName()/setFontName()` + `textStr()/fontNameStr()`
> - ✅ `SyBarCode`/`SyQRCode`：`strData` → private，`data()/setData()` + `dataStr()`
> - ✅ `SyLine`：`vPoints` → private，`pointCount()/points()/pointAt()/setPointAt()/setPoints()/addPoint()/clearPoints()/reservePoints()` + `pointRef()/mutablePointRef()/pointVector()/setPointVector()`
> - ✅ `SyPolygon`：`m_vVertices` 私有 + `vertices()/setVertices()`
> - ✅ `SySmartLine`：`m_vSegments` 私有 + `addSegment(SyEntity*)/segment()/releaseSegments(SyEntity**, size_t*)` + 内联 `addSegmentUnique()`
> - ✅ `SyImage`：`vPixelData` 私有 + `pixelData()/pixelDataSize()/setPixelData()/clearPixelData()` + `pixelDataVector()/setPixelDataVector()` 包装
> - ✅ `SyNurbs`：`vControlPoints`/`vKnots`/`vWeights` → private，`controlPoints()/controlPointAt()/setControlPointAt()/addControlPoint()/knots()/knotAt()/addKnot()/weights()/weightAt()/addWeight()` 等 POD 访问器 + `*Ref()/*Vector()/set*Vector()` 便捷包装
> - ✅ `SyGroup`：`name` → private `m_name`，`name()/setName()` + `nameStr()/setNameStr()`

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `SyEntity/SySmartLine.h` | ~~38,49~~ | ~~`unique_ptr`/`vector<unique_ptr>` 参数/返回~~ | ~~参数/返回/成员~~ | ✅ **已收口（批次 D 补全）**：`addSegment(std::unique_ptr<SyEntity>)` → `addSegment(SyEntity*)`（接管所有权）；`releaseSegments()` → `SyEntity** releaseSegments(size_t*)`；私有 `vector<unique_ptr> m_vSegments` 仅在 DLL 内使用（析构已分离至 .cpp）；header 内联 `addSegmentUnique(unique_ptr)` 便捷包装 |
| `SyEntity/SyText.h` | ~~43,45,62~~ | ~~`string`×2/`unique_ptr`~~ | ~~成员/返回~~ | ~~中/高~~ | ✅ **已收口（批次 D）**：`strText`/`strFontName` 已私有化，`text()/setText()/fontName()/setFontName()` 提供 `const char*` 访问器；`textStr()/fontNameStr()` 作为 header-inline 便捷包装，不跨 DLL |
| `SyEntity/SyImage.h` | ~~38~~ | ~~`vector<unsigned char> vPixelData` public 成员~~ | ~~成员~~ | ✅ **已收口（批次 D 补全）**：`vPixelData` 私有化；新增 `pixelData()/pixelDataSize()/setPixelData()/clearPixelData()` POD 访问器（`setPixelData`/`clearPixelData` 定义于新建 `SyImage.cpp`）+ `pixelDataVector()/setPixelDataVector()/pixelDataRef()/mutablePixelDataRef()` 内联包装 |
| `SyEntity/SyLine.h` | ~~31,40,42~~ | ~~`vector<Vec2d>` 构造+`vPoints` 成员 / `unique_ptr`~~ | ~~参数/成员/返回~~ | ~~中/高~~ | ✅ **已收口（批次 D）**：`vPoints` 私有化；提供 `pointCount()/points()/pointAt()/setPointAt()/setPoints()/addPoint()/clearPoints()/reservePoints()` + `pointVector()/setPointVector()` 便捷（header-inline） |
| `SyEntity/SyPolygon.h` | ~~35-37,46,53-54~~ | ~~`vector<Vec2d>`~~ | ~~返回/参数~~ | ~~中~~ | ✅ **已收口（批次 D）**：`m_vVertices` 私有化 + `vertices()/setVertices()` POD 访问器 |
| `SyEntity/SyNurbs.h` | ~~尾部~~ | ~~`vector`（控制点/权重/节点）~~ | ~~成员~~ | ~~中~~ | ✅ **已收口（批次 D）**：控制点/权重/节点私有化 + `controlPoints()/knots()/weights()` 等 POD 访问器 |
| `SyEntity/SyBarCode.h`/`SyQRCode.h` | ~~21,27/21,26~~ | ~~`string strData`/`unique_ptr`~~ | ~~成员/返回~~ | ~~中/高~~ | ✅ **已收口（批次 D）**：`strData` 私有化，`data()/setData()` + `dataStr()` 便捷包装 |
| `SyEntity/SyBezier2/SyBezier/SyArc/SyCircle/SyPoint/SyEllipse.h` | ~~—~~ | ~~`unique_ptr` clone~~ | ~~返回~~ | ~~高~~ | ✅ **已收口（批次 D）**：`clone()` 改为返回裸指针 `SyEntity*` + 虚 `destroy()`，统一通过 `clonePreserveId` 在 DLL 内部克隆 |
| `Core/SceneManager.h` | 109-192 | `vector<T>`/`VecSyEntityPtr`/`vector<unique_ptr>`/`unordered_set`/`vector<IEntity*>`/`vector<Fio::GroupInfo>` | 模板/参数/返回 | 高 | ✅ **部分修复**：`addEntities(const VecSyEntityPtr&)` (被 NestingEngine 跨 DLL 调用) 新增 C-safe 重载 `addEntities(SyEntity* const*, size_t)`，NestingEngine 迁用 `.data()/.size()`；其余方法 (`extractEntitiesById`/`replaceEntitiesById`/`insertEntitiesPreserveId` 等) 仅供 Engine2D 内部调用，标注为内部 C++ ABI，未暴露给跨 DLL 调用方 |
| `Core/EntityContainer.h` | 43-55 | `VecSyEntityPtr`/`unique_ptr`/`vector<unique_ptr>` | 返回/参数 | 高 |
| `Core/GroupManager.h` | 42-54 | `const string&`/`vector<SyEntity*>` | 参数/返回 | 高 |
| `Core/SelectionManager.h` | 30-48 | `VecSyEntityPtr` | 参数/返回 | 高 |
| `Core/EntityClipboard.h` | 全 | 疑似 STL | 待确认 | 中 |
| `Core/DocumentTransaction.h` | 57-63 | `unique_ptr`×2 + `string` | public 成员 | 高/中 |
| `Core/SceneChangeSet.h` | 19-30 | `vector<unique_ptr>`/`vector<EntityId>`/`vector<EntityReplace>` | public 成员 | 中 |
| `Edit/GeometryEditResult.h` | 35-52 | `unique_ptr`/`optional<string>`×2 | public 成员 | 中/高 |
| `Edit/SceneUndoCommands.h` | ~~17-43~~ | ~~`vector<unique_ptr<SyEntity>>`~~ | ~~返回/构造~~ | ~~高~~ | ✅ **已修复**：命令类 (`EntitySnapshotsCommand`/`DeleteEntitiesCommand`/`AddEntitiesCommand`) 去除 `ENGINE2D_API` 导出（DLL 内部构造保留）；新增导出工厂 `createEntitySnapshotsCommand`/`createDeleteEntitiesCommand`/`createAddEntitiesCommand`（入参为 POD 指针数组 + 计数）；`captureEntitySnapshots` 新增 C-safe 导出版本（写入调用方缓冲 `SyEntity**`，返回计数）；外部调用方 (UI2D ArrayOperations/AlgorithmTaskRegistration, 主测 UndoRedoRegressionTests) 全面迁用工厂 + C-safe capture |
| `Edit/SceneEditService.h` | 53-70,88-117 | `unique_ptr`/`vector<unique_ptr>`/`vector<EntityId>`/`string`/`std::function` | 参数/返回/回调 | 高 |
| `Edit/UndoRedoManager.h` | 64,110-113,129-130 | `unique_ptr` 构造 / `std::function` 回调×2 | 构造/回调 | 高 |
| `Edit/LayerEditService.h` | 40-97 | `vector<EntityId>`/`vector<SyEntity*>`/`const string&`/`unordered_map` | 参数 | 高 |
| `Edit/LayerSnapshot.h` | 11-30 | `string`/`unordered_map`/`vector<int>` | public 成员 | 中 |
| `Environment/SceneEnvironment.h` | 24,37-43 | `std::function<void()>` | typedef/setter | 高 |
| `Environment/SceneEnvLayer.h` | 19,35-37 | `vector<Vec2f>`/`vector<SceneEnvLayer>`/文本列表 | public 成员 | 中 |
| `Interaction/LayerManager.h` | 42-53 | `const string&` | 参数 | 高 |
| `Interaction/HardwareProfileManager.h` | 10-19,48-53 | `string`/`const string&` | 成员/参数 | 中/高 |
| `Interaction/SnapEngine.h` | 78,83,89 | `std::function<vector<...>()>` | 回调 typedef/setter | 高 |
| `Interaction/GridSnapManager.h` | 121,122,135,136,143,161,162 | `vector<double>`/`std::function`/`SettingsData&`(含 variant) | 参数/回调 | 高 |
| `Render/DisplayMesh.h` | 55,56 | `vector<DisplayVertex>`/`vector<DisplayDrawRange>` | public 成员 | 中 |
| `Render/Tessellator.h` | 44,62 | `vector<Vec2d>`/`vector<TessellatedEntity>` | public 成员 | 中 |
| `Render/EntityTessellateUtil.h` | 26-41 | `vector<Vec2f>&`/`VecSyEntityPtr` | 参数/返回 | 高 |
| `Render/DisplayCache.h` | 58-61 | `DisplayMesh`（含 vector） | 返回 | 高 |
| `Render/SelectionOutlineBuilder.h` | 19,30-31 | `vector<SelectionOutlineVertex>` | 成员/参数 | 中/高 |
| `Algorithm/ArrayAlgorithm.h` | 21-23 | `vector<const SyEntity*>` | 参数 | 高 |
| `Algorithm/EntityTransform.h` | 28-50 | `const vector<EntityId>&` | 参数 | 高 |
| `Algorithm/EntityFiller.h` | 32 | `vector<SyEntity*>` | 参数 | 高 |
| `Algorithm/Geo2DEdit.h` | 36-48 | `vector<unique_ptr<SyEntity>>` | 返回 | 高 |
| `Algorithm/Geo2DSampling.h` | 51-53 | `vector<Vec2d>`/`vector<double>`/`vector<int>` | public 成员 | 中 |
| `Algorithm/PathOptimizer.h` | 31-49 | `vector<Vec2d>`/`vector<size_t>` | 参数/返回 | 高 |
| `Algorithm/Boolean/EntityBoolean.h` | 23-25 | `vector<SyEntity*>` | 参数 | 高 |
| `Algorithm/Offset/EntityOffset.h` | 21-23 | `vector<SyEntity*>` | 参数 | 高 |
| `Algorithm/Discretizer/EntityDiscretizer.h` | 25-28 | `vector<Vec2dVector>&` | 参数 | 高 |
| `Algorithm/Fill/LinesIntersection.h` | 24-55 | `VecLines`/`unique_ptr` | 参数/返回 | 高/低 |
| `Algorithm/Fill/EntityFill.h` | 32,41 | `VecLines&`/`VecLines*` | 参数/返回 | 高 |
| `Algorithm/INestingJobRunner.h` | 38-49+尾部 | `string`/`vector<SyEntity*>`/`vector<Vec2d>` | public 成员 | 中/高 |
| `Algorithm/NestingEngine.h` | 21-42 | `vector<NestingPartInput>`/`NestingJobResult`/`std::function` | 参数/返回/回调 | 高 |
| `Algorithm/NestingPartPreparer.h` | 22-48 | `vector<NestingPartInput>` | 成员/参数 | 高 |
| `Algorithm/ArrayParams.h` | 57,78,80 | `std::function`/`vector<unique_ptr>`/`string` | 间接跨 DLL 数据 | 高 |

#### 3.3 Engine/3D（ENGINE3D_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `SceneManager3D.h` | — | 已收口（PIMPL，回调式访问器 + 裸指针数组 + C 函数指针） | 头文件无 STL | ✅ 已修复 |
| `SyEntity/SyMeshEntity.h` | 35,38 | `vector<Vec3f>` vertices/normals | public 成员 | 中 |
| `Edit/SceneUndoCommands3D.h` | — | 已收口（PIMPL，构造改裸指针数组 + 扁平数组） | 头文件无 STL | ✅ 已修复 |
| `Geometry/Geo3DQuery.h` | 33-50 | `vector<RayHit3D>`/`vector<SyMeshEntity*>&` | 返回/参数 | 高 |
| `Geometry/Geo3DParallel.h` | 25-47 | 全 `vector` | 参数/返回 | 高 |
| `Selection/SelectionManager3D.h` | 49,72 | `vector<EntityPtr>` typedef | typedef/返回 | 高 |
| `SpatialIndex/SpatialIndex3D.h` | 39,71 | `vector<EntityPtr>` | typedef/参数 | 高 |
| `Loader/ObjLoader.h` | 24-45 | `string`/`unique_ptr` | 参数/返回 | 高 |
| `Loader/StlLoader.h` | 28-47 | `string`/`unique_ptr` | 参数/返回 | 高 |
| `Algorithm/DataConverter.h` | 22,36-49 | `VecSyEntityPtr&`/`vector<Vec2f>&` | 参数 | 高 |

#### 3.4 UI/Common（UICOMMON_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `Persistence/SettingsTable.h` | 13,27-51,57-67 | `variant`/`string`/`map` | 全接口 | **高（Top1）** | ✅ **已修复**：PIMPL 隐藏 map/variant；导出方法全改 `const char*`/缓冲区；`setValue/getValue(variant)`、`values()` 移除，改 `forEach(callback,ctx)`；header 内联便捷包装保持调用方兼容 |
| `UiDialogHelpers.h` | 17,19 | `pair<QString,QVariant>`/`vector<ComboItem>` | typedef/参数 | 中/高 |
| `Dlg/ProgressDialog.h` | 62,84,132,155 | `shared_ptr`/`std::function` | 构造/参数 | 高 |
| `Progress/ProgressTracker.h` | 20,32,34,43,88-124 | `string`/`std::function` | 成员/typedef/参数 | 高 |
| `Dlg/SettingsDialogBase.h` | 50,52,65,85,103 | `vector<ISettingsTab*>`/`std::function<unique_ptr>`/`string` | 参数/typedef | 高 |
| `Dlg/EntityPropertiesDialogBase.h` | 52,56,117 | `unique_ptr<IPropertyProvider>` | 参数/成员 | 高 |
| `Algorithm/AlgorithmTaskTypes.h` | 52,60,66 | `std::function`×3 | 导出 struct 成员 | 高 |
| `Algorithm/AlgorithmApplicationService.h` | 26,44 | `unique_ptr`/`unordered_map` | 参数/成员 | 高 |
| `Plugin/CommandPluginRegistry.h` | ~~21,22,38~~ | ~~`shared_ptr`/`QList<shared_ptr>`~~ | ~~参数/返回~~ | ~~高~~ | ✅ **已修复**：注册接口 `registerPlugin(std::shared_ptr)` → `registerPlugin(ICommandPlugin*, void(*destroy)(ICommandPlugin*))`；`m_plugins` 由 `QList<std::shared_ptr>` 改为 `QList<PluginEntry>`（存储裸指针 + 销毁回调）；析构函数调用 `destroy` |
| `Plugin/CommandPluginRegistryApi.h` | ~~15~~ | ~~`shared_ptr`~~ | ~~自由函数参数~~ | ~~高~~ | ✅ **已修复**：`commandPluginRegistryRegister` 签名改为 `(ICommandPlugin*, void(*)(ICommandPlugin*))` |
| `Menu/ContextMenuBuilder.h` | 79,138 | `std::function`×2 | 参数 | 中 |
| `Render/RenderTypes.h` | 约 20 处 | `vector` 结构体成员 | 成员 | **高（Top2）** |

#### 3.5 UI/2D（UI2D_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `RenderWidget.h` | 96, setSceneCommands | `vector<SelectionOutlinePath>`/`RenderCommandList&&` | 参数 | 高 |
| `Operation/InteractiveEditSession.h` | 19,20 | `vector<SyEntity*>`/`const string&` | 参数 | 高 |
| `Operation/AlgorithmRunner.h` | 46 | `std::function<void()>` | 参数 | 高 |
| `Operation/OperationRouting.h` | 23,26 | `std::function<bool(...)>` | typedef/setter | 高 |
| `Operation/OperationRegisterSink2D.h` | 28 | `unique_ptr<IOperation>` | 参数 | 中 |

#### 3.6 UI/3D（UI3D_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `Edit/UndoRedoManager3D.h` | 54,73,74 | `unique_ptr`/`vector<unique_ptr>` | 参数/成员 | ✅ 已收口（C1）：PIMPL（`Impl*`，stacks+sceneManager 下沉 .cpp），`pushCommand` 改裸指针（接管所有权），`IUndoCommand3D::description()` 改 `const char*`，头文件不再依赖 STL |
| `Edit/UndoCommands3D.h` | 26-51 | `unique_ptr` 返回 / `vector<unique_ptr>` 参数 | 工厂函数 | 高（makeTransform 参数已 POD 化；C1 起 3 个命令 `description()` 已随接口改 `const char*`；makeDelete/makeSceneReplace 同 DLL 内部保留） |
| `Storage/SceneDocumentIO3D.h` | 28-32 | `vector<unique_ptr<SyMeshEntity>>` | 返回/参数 | 高 |
| `Adapter/GmcMeshBridge3D.h` | 36,43,49 | `unique_ptr` 返回 | 静态方法 | 中 |
| `Adapter/EngravingMeshBridge3D.h` | 29 | `const vector<SyMeshEntity*>&` | 参数 | 高 |
| `Operation/IOperation3D.h` | 49,72 | `std::function` | typedef/成员 | 高 |
| `Operation/OperationRegisterSink3D.h` | 28 | `LambdaOperation3D::Fn`(std::function) | 参数 | 高 |
| `Operation/UiStateBridge3D.h` | 22 | `const vector<SyMeshEntity*>&` | 参数 | 高 |
| `Service/BRepModelService3D.h` | 60-68 | `unique_ptr` 返回×3 | 静态方法 | 中 |
| `Relief/ReliefEngravingJob3D.h` | 23,24 | `ToolpathList`/`vector<float>` | 导出 struct 成员 | 高 |
| `Relief/ReliefEngravingProcessor3D.h` | 31,49 | `vector<SyMeshEntity*>`/`ToolpathList` | 参数/返回 | 高 |
| `Relief/ReliefHardwareDispatcher3D.h` | 37,40,43 | `ReliefEngravingJob3D&`（含 STL 成员） | 参数 | 中 |
| `Relief/ReliefToolpathExporter3D.h` | 19,21 | `ReliefEngravingJob3D&` | 参数 | 中 |

#### 3.7 FileIO（FILEIO_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `IFileParser.h` | 18,36 | `VecSyEntityPtr`/`ParseResult` | 纯虚接口 | ✅ 已收口（C4）：`ParseResult`/`WriteResult` 已内迁至 `Src/Internal`（`ILegacyParser`/`ILegacyWriter`），不再经导出接口携带 STL 跨 DLL 边界；`VecSyEntityPtr` 仅为 Main 侧兼容别名（using，无 ABI 影响）；析构定义移入 .cpp；`forEachSupportedExtension(callback,ctx)`+`formatName(buffer,size)` 替代 STL 返回 |
| `IFileWriter.h` | 24 | `VecSyEntityPtr`/`WriteResult` | 纯虚接口 | ✅ 已收口（C4）：`WriteResult` 已内迁至 `Src/Internal`（`ILegacyWriter`）；析构定义移入 .cpp；`formatName(defaultExtension` 改 buffer 模式 |
| `FileIOManager.h` | ~~18-43~~ | ~~`std::function` 回调/`const string&`/`VecSyEntityPtr`/`vector<string>`~~ | ~~全接口~~ | ✅ 已收口（C1）：回调改 C 函数指针+`void* ctx`，`importFile/exportFile` 改 `const char*`+裸指针数组+`errorBuffer`，新增 `deleteEntities/freeEntityArray/supportedImportExtensions/canImport/canExport`，头文件不再依赖 IFileParser/IFileWriter/STL |
| `FileWriterFactory.h` | ~~18-38~~ | ~~`std::function<unique_ptr>`/`unique_ptr`/`vector<FileFormat>`~~ | ✅ 已收口（C2）：PIMPL（`Impl*`），`CreatorFunc` 改 C 函数指针，`createWriter` 返回裸指针 + `destroyWriter`，`supportedFormats`/`supportedExtensions` 改 `forEachSupportedExtension(callback,ctx)`，字符串改 `const char*` |
| `FileParserFactory.h` | ~~18-52~~ | ~~`std::function<unique_ptr>`/`unique_ptr`/`vector`/`map`~~ | ✅ 已收口（C2）：PIMPL（`Impl*`），`CreatorFunc` 改 C 函数指针，`createParser`/`createParserByExtension` 返回裸指针 + `destroyParser`，扩展名枚举改 `forEachSupportedExtension(callback,ctx)`，字符串改 `const char*` |
| `SyDocument.h` | ~~30-134~~ | ~~`PropertyMap`/`string`×7/`vector<LayerInfo>`/`vector<unique_ptr>`/`vector<GroupInfo>`/`map`/`vector<string>`~~ | ✅ 已收口（C3）：全 PIMPL（`SyDocumentData*` + friend 内部访问器），STL 结构体（`DocumentMetadata`/`LayerInfo`/`HardwareInfo`/`GroupInfo`/`PropertyMap`）与全部容器成员内迁 `Src/Internal/SyDocumentData.h`；元数据改 `const char*` getter/setter，图层改 POD 视图 `SyLayerInfo`（`addLayer/getLayerAt/layerCount`），硬件改 POD 视图 `SyHardwareInfo`（`setHardware/getHardware`），图元改借用指针（`entityAt`/`addEntity` 接管所有权），`SyDocumentPtr` 已删除；消费方已同步（SySerializer/NativeParser(3D)/FileManager/各测试） |
| `SyDocument3D.h` | ~~20~~ | ~~`vector<unique_ptr<SyMeshEntity>>`~~ | ✅ 已收口（C3）：全 PIMPL，公开头不再 include Engine3D；`meshEntityCount/meshEntityAt/addMeshEntity` 访问器 |
| `SySerializer.h` | ~~30-155~~ | ~~`string`/`vector<string>`/`vector<uint8_t>`/`CryptoProviderPtr`~~ | ✅ 已收口（C3）：`SerializeResult` 改纯 POD（`char errorMessage[512]`），`serializeToMemory` 改 `BinaryBlobOut*` 两段式（data=nullptr 仅查询大小），`deserializeFromMemory` 改 `BinaryBlob` 入参，文件路径统一 `const char*`，警告改 C 函数指针回调（`SerializeWarningCallback`+`void* ctx`），`setCryptoProvider` 保留裸指针，加密提供者经 PIMPL 隐藏，头文件不再依赖 STL；消费方已同步（FileManager/FileImporter/NativeParser/NativeParser3D/NativeWriter/NativeWriter3D/各测试） |
| `FileIOError.h` | 14-59 | `string`/`vector<string>`/`vector<DxfLayerInfo>`/`map` | 导出结构体 | ✅ 已删除（C4）：错误定义已随 `ParseResult`/`WriteResult` 内迁至 `Src/Internal/FileIOInternal.h`，头文件不再导出 |

### 4. 全局结论

- 绝大多数已识别的跨 DLL STL 违规项已完成收口，工程进入“已治理、待决策、可持续收尾”阶段。
- 剩余的 Hardware Qt 类型边界（`QString` / `QStringList` / `QList`）属于**架构决策项**，不再是常规 ABI 修复任务。
- 后续若新增 public header，默认按本节判定规则执行；若无法避免 STL，应先判断该头文件是否真的是跨 DLL 稳定边界。

### 5. 架构决策记录（ADR）—— Hardware Qt 类型跨 DLL 边界

- **决策主题**：Hardware 模块是否允许 `QString` / `QStringList` / `QList` 作为跨 DLL public API 契约。
- **问题背景**：Hardware 已完成常规 ABI 收口，但 Qt 类型仍可能在对外接口上形成“编译器/Qt 版本/构建体系”耦合。
- **候选方案**：
  1. 允许 Qt 类型跨 DLL，保持当前开发便利；
  2. 收口为 POD / `const char*` / 回调 / buffer，禁止 Qt 直出；
  3. 仅在 Hardware 内部 DLL 之间允许 Qt，其他模块禁用。
- **推荐方向**：优先采用“**内部允许、跨模块不承诺**”的过渡策略，即：Hardware 可以继续使用 Qt 类型完成内部协作，但 public header 需明确标注仅限同编译器/同构建体系；若未来需要对外插件或跨编译器兼容，再升级为 POD 化契约。
- **决策状态**：待产品/架构确认（保留现状，记录风险，不作为常规 ABI 修复项）。

| `SyCryptoProvider.h` | 93,116 | `unique_ptr` typedef/`vector<uint8_t>` | typedef/成员 | 中/低 |
| `ImageUtils.h` | ~~26~~ | ~~`const string&`~~ | ~~自由函数参数~~ | ~~高~~ | ✅ **已修复**：`readImageInfo` 签名改为 `const char* strUtf8Path`；调用端 `SyDocumentData` 改为 `filePath.toUtf8().constData()` |
| `FileIOUtils.h` | 31,77,81,87-89 | `const string&`×3/`string`×3 | 构造/返回/成员 | 中 |
| `Parsers/NativeParser3D.h` | ~~29-30~~ | ~~`const string&`/`SyDocument&`~~ | 参数 | ✅ 已收口（C3）：`parseDocument(const std::string&)` → `const char*`，警告经回调收集 |
| `Writers/UgWriter.h` | 33 | `string parameterData` | 私有嵌套结构体 | 低 |

#### 3.8 GeoModelCore（GEOMODEL_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `TopoShape.h` | 49-138 | `shared_ptr<TopoShape>`/`vector<shared_ptr>`/`vector<double>`/`unique_ptr<TopoShapeImpl>` | 返回/参数/PIMPL | 高/低 |
| `GmcCurve.h` | 67-117 | `shared_ptr<GmcCurve>`/`vector<double>`/`vector<int>`/`unique_ptr` | 返回/参数 | 高 |
| `GmcSurface.h` | 60-108 | `shared_ptr<GmcSurface>`/`shared_ptr<GmcCurve>`/`unique_ptr` | 返回 | 高 |
| `GmcBoolean.h` | 20-53 | `shared_ptr`/`vector<shared_ptr>` | 静态方法返回 | 高 |
| `BRepBuilder.h` | 23-66 | `shared_ptr`/`vector<shared_ptr>` | 静态方法 | 高 |
| `GmcTopoExplorer.h` | 64-104 | `shared_ptr`/`vector<shared_ptr>`/`pair`/`string` | 静态方法 | 高 |
| `GmcShapeHealing.h` | 52-86 | `string`/`shared_ptr`/`vector<shared_ptr>`/`string*` | 静态方法 | 高 |
| `GmcLaw.h` | 63-124 | `vector<CurveSample>`/`vector<SurfaceSample>`/`vector<PathPoint>` | 静态方法 | 高 |
| `GmcSplit.h` | 26-30 | ~~`string`~~/`shared_ptr` | 返回结构体 | ~~高~~ → ✅ `errorMessage` 改 `char[256]` |
| `GmcTypes.h` | 106-147 | `vector<double>`×2/~~`string`~~ | 返回结构体 | ~~高~~ → ✅ `errorMessage` 改 `char[256]` |
| `GmcQuery.h` | 44 | `string` | 返回 | 高 |
| `GmcProjection.h` | 14,25 | `vector<pair>`/`vector<GmcPolyline2d>` | 结构体成员/返回 | 高 |
| `GmcBvh.h` | 38 | `vector<GmcMeshIntersectionResult>` | 返回 | 高 |
| `GmcIntersection.h` | 31 | `vector<GmcMeshIntersectionResult>` | 静态方法返回 | 高 |
| `GmcMesh.h` | 16,22 | `GmcMeshData`/~~`const string&`~~ | 返回/参数 | ~~高~~ → ✅ `exportStl` 改 `const char*` |
| `GeoModelDocument.h` | 26-40 | `const string&`×2/`string`/`shared_ptr`/`unique_ptr` | 参数/返回 | 高 |
| `GmcKernel.h` | 39 | `shared_ptr<TopoShape>` | 静态方法返回 | 高 |

#### 3.9 Engraving（ENGRAVING_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `LaserStrategy.h` | 41,42,79 | `using Toolpath=vector<ToolpathPoint>`/`ToolpathList`/`LaserStrategyPtr=shared_ptr` | typedef | 高 |
| `LaserProcessingAPI.h` | 17-54 | `shared_ptr`×5/`ToolpathList`/`PointCloud`/`TriangleMesh`/`cv::Mat` | 静态方法 | 高 |
| `VolumeProcessing.h` | 22-106 | `PointCloud`/`TriangleMesh`/`LaserStrategyPtr`/`ToolpathList`/`vector<float>`/`vector<vector<Point3D>>` | 全接口 | 高 |
| `LayeredProcessing.h` | 20-70 | `HeightMapPtr`/`LaserStrategyPtr`/`ToolpathList`/`vector<float>`/`Toolpath`/`vector<vector<cv::Point>>` | 全接口 | 高 |
| `MeshSlicer.h` | 33-58 | `vector<SliceSegment>`/`vector<vector<Point3D>>`/`ToolpathList` | 静态方法 | 高 |
| `HeightMap.h` | 60 | `HeightMapPtr` | typedef | 中 |
| `EngravingImageAPI.h` | 13 | `ToolpathList` | 返回 | 高 |

#### 3.10 Log（LOG_API）

| 文件 | 行号 | STL 类型 | 位置 | 风险 | 状态 |
|---|---|---|---|---|---|
| `SyTraceContext.h` | 15,19,22 | `const string&`/`string` | 导出自由函数 | 高 | ✅ **已修复**：改 `const char*` + 调用方缓冲区；`currentTraceIdString`/`resolveTraceIdString` 为 header 内联包装 |
| `SyLogger.h` | 26-27 | `std::string`（内联转换结构体） | 头内结构体 | 低 | 保留（不跨 DLL 边界） |
| `SyLogger.h` | 97-100 | `const string&` Initialize 重载 | 导出方法 | 中 | ✅ **已修复**：重载删除 |
| `SyLogger.h` | 113-114 | `using LogPathCallback=std::function<string()>` | typedef/setter | 高 | ✅ **已修复**：改 `const char* (*)(void* ctx)` + ctx |
| `SyLogger.h` | 133 | inline `LogSrc(..., const string&)` | inline 方法 | 低 | 保留（header 内联，编译进调用方） |

#### 3.11 Hardware / Network

| 文件 | 行号 | STL 类型 | 位置 | 风险 |
|---|---|---|---|---|
| `Hardware/ToolpathExecutor.h` | ~~25~~,49 | ~~`const vector<HardwareToolpathPoint>&`~~ | 参数（已收口）/成员 | ✅ 参数已收口 pointer+count；私有成员 `m_points` 析构已分离至 .cpp（模式5） |
| `Hardware/LaserController.h` | ~~98~~ | ~~`const vector<HardwareToolpathPoint>&`~~ | 参数 | ✅ 已收口 pointer+count，`#include <vector>` 已移除 |
| `Network/RetryPolicy.h` | 123-148 | `std::function<void(QNetworkReply*)>` | 回调参数 | 🟡 已标注内部 ABI（当前孤立未被跨 DLL 消费，待对外开放时收口） |

> 附加：Hardware/Network 以 Qt 类型（QString/QStringList/QJsonObject）作为跨 DLL 接口参数，依赖 Qt 二进制兼容，另需决策。

#### 3.12 Vision（沙箱模块）

全模块约 100+ 处 `std::string/vector/shared_ptr/cv::Mat/cv::Point`。按约定标注为"沙箱内部模块，不承诺跨 DLL ABI"，在 `VisionAPI.h` 显式声明即可，不做收口。

### 4. Qt 类型跨 DLL 依赖（非 STL，单独跟踪）

- UI/Common：`BaseMenu`/`IMenu`/`IPropertyProvider`/`IMaterialRepository`/`IShortcutSettingsModel`/`ThemeManager`/`LanguageManager`/`ITool3D` 大量 `QList`/`QHash`/`QVector`/`QStringList`/`QVariantMap`。
- UI/2D：`RenderWidget.h` QString 返回、`IOperation.h` OperationRequest 含 `QVariantMap`、`OperationRouting.h` QVariantMap 参数。
- UI/3D：`OperationId3D`/`OperationRouting3D` QVariantMap、`NavigationConfig3D`/`ShortcutManager3D` QStringList/QVector（`IUndoRedoCommand3D::description()` 已随 C1 改 `const char*`）。

### 5. 建议执行节奏（排期参考 — 历史记录）

| 批次 | 内容 | 预期效果 | 状态 |
|------|------|----------|------|
| **A** | Log（SyTraceContext/SyLogger）+ UI/Common SettingsTable | 改动小、影响面最大（被 7+ 模块消费） | ✅ 全部完成 |
| **B** | FileIO 核心链（FileIOManager/SyDocument/SySerializer） | 覆盖 Main 直接消费的导入导出链路 | ✅ 全部完成 |
| **C** | UI/3D + Engine/3D（UndoCommands3D/SceneDocumentIO3D/SceneManager3D） | 延续 3D 收口方向 | ✅ 全部完成 |
| **D** | Engine/2D 图元族（clone 返回 unique_ptr + public vector 成员） | 覆盖面最大 | ✅ 已完成 |
| **E** | GeoModelCore（对外承诺 PIMPL 句柄 + POD） | 量大 | ✅ 已完成 |
| **F** | Hardware/Network 函数指针回调 + pointer+count | 小改 | ✅ 已完成（Qt 类型待决策） |
| **G** | Vision 标注沙箱 | 无需改动 | ✅ 已完成 |

### 6. 最终状态表（按模块）

| 模块 | 状态 | 结论 |
|---|---|---|
| Log | 已完成 | 公开 ABI 已收口，剩余仅为内部实现维护 |
| UICommon | 已完成 | `SettingsTable` / 相关业务接口已收口 |
| FileIO | 已完成 | `FileIOManager`、工厂、`SySerializer`、`SyDocument` 链路已收口 |
| Engine/Common | 已完成 | 基础抽象与图元基类已收口 |
| Engine/2D | 已完成 | 图元族、编辑、撤销链路已完成收口 |
| Engine/3D | 已完成 | 3D 编辑、BRep、撤销、场景链路已完成收口 |
| UI/2D | 已完成 | `IEntityEditor`、`EntityEditorFactory` 等已收口 |
| UI/3D | 已完成 | 视口、工具、BRep、场景链路已收口 |
| GeoModelCore | 已完成 | 导出函数、结果结构、PIMPL 已收口 |
| Engraving | 已完成 | 虚接口与导出 API 已收口 |
| Hardware | 基本完成 | `loadToolpath` 已收口；Qt 类型边界待决策 |
| Network | 已完成/内部可接受 | 仅需保留内部 ABI 标注 |
| Nesting | 已完成 | 句柄化已完成 |
| PyBindCore | 已完成 | facade 路线已完成 |
| Render/Common Qt（旧，已删除） | 已完成 | 已完成必要收口；旧渲染库已删除，统一为 Renderx/SanYiRender |
| Vision | 内部可接受 | 沙箱模块，不承诺跨 DLL ABI |

### 7. 代码级复核清单（✅ 已完成）

#### 7.1 高优先复核结果（✅ 全部完成）

| 优先级 | 文件/目录 | 原因 | 复核结果 |
|---|---|---|---|
| 高 | `Hardware/Hardware/Include/...` | 唯一仍需决策的 Qt 类型边界 | ✅ 复核通过：STL 私有成员析构已分离；Qt 类型边界属架构决策 |
| 高 | `GeoModelCore/GeoModelCore/Include/...` | 高复杂度模块 | ✅ 复核通过：GmcLaw/GmcBvh/GmcKernel 补齐 ABI 注解；其余已有注解 |
| 高 | `UI/3D/Include/UI3D/...` | 公共接口较多 | ✅ 复核通过：SceneDocumentIO3D/ToolContext3D/IRenderer3D/ReliefEngravingProcessor3D/EngravingMeshBridge3D 补齐注解 |
| 高 | `UI/2D/Include/UI2D/...` | 编辑/工具/工厂接口密集 | ✅ 复核通过：InteractiveEditSession/RenderWidget/ServiceFactory/AlgorithmRunner/IOperation/OperationRouting 补齐注解 |
| 高 | `Engine/2D/Include/Engine2D/...` | 图元族收口后确认 | ✅ 复核通过：TextConverter/HardwareProfileManager/GridSnapManager/ArrayAlgorithm/LayerManager 补齐注解；Engine2DAPI.h 添加模块级声明 |
| 中 | `Engine/Common/Include/Engine/...` | 基础层 | ✅ 复核通过：FontParameters/FontManager/SyLayer/SceneGeometryCollector/TextItem/Utf8Text/FontUtil 补齐注解 |
| 中 | `FileIO/FileIO/Include/...` | 历史复杂度高 | ✅ 复核通过：ImageUtils/FileIOUtils/PdfToSvgConverter/PltHpglInterpreter 补齐注解 |
| 中 | `Engraving/Engraving/Include/...` | 确认无回退 | ✅ 复核通过：VolumeProcessing/MeshSlicer/LayeredProcessing 补齐注解 |
| 低 | `Vision/Vision/Include/...` | 沙箱模块 | ✅ 已确认：VisionAPI.h 已有沙箱声明，覆盖全部子文件 |
| 低 | `Renderx/...` | 内部实现 | ✅ 已确认：公共头已是 C ABI（extern C + POD），无 STL 泄漏 |
| 低 | `Engine/3D/Include/Engine3D/...` | 内部 C++ DLL | ✅ 复核通过：Engine3DAPI.h 添加模块级声明；SyMeshEntity/SceneManager3D/SceneUndoCommands3D 已有注解 |
| 低 | `Log/Log/Include/...` | 确认无回退 | ✅ 已确认：无回退 |
| 低 | `Nesting/Nesting/Include/...` | 确认无新加 STL | ✅ 已确认：0 处 STL 匹配 |
| 低 | `PyBindCore/PyBindCore/Include/...` | 确认 facade 没有绕过 | ✅ 已确认：DocumentFacade/FacadeTypes/SceneGateway 均已注解 |

#### 7.2 复核要点与结论

- public 头文件中是否再出现 `std::string` / `std::vector` / `std::unique_ptr` / `std::shared_ptr` / `std::function`
- 纯虚接口是否还有容器或字符串直接穿越 DLL
- 导出类是否仍有 STL 私有成员未 PIMPL
- 析构函数是否仍有内联定义，导致 STL 成员跨边界销毁
- 是否有新增 `QString` / `QStringList` / `QList` 出现在跨 DLL public API 中

#### 7.3 总结判断

- 已知 ABI 修复项：**已完成**
- 最终复核（ABI 标注补齐）：**✅ 已完成** — 高优先 26 文件 + 中优先 7 文件 + 模块级声明 2 文件
- 析构分离审计：**✅ 已完成**（2026-08-02，两轮）— 第一轮 8 个 + 第二轮 20 个 = 共 28 个导出类/接口 `= default` 内联析构已全部下沉至 `.cpp`（详见 §7.4、§7.6）
- 仍需架构决策项：**Hardware Qt 类型边界**（唯一保留项）
- 仍值得盯一遍的文件：**无** — 全模块 public 头文件已复核完毕，STL 使用均有 ABI 注解覆盖

#### 7.4 析构分离最终审计（2026-08-02）

全面扫描所有 `ENGINE2D_API` / `ENGINE3D_API` / `UI3D_API` 导出类，发现 8 个类/接口仍保留 `= default` 内联析构，其 STL/Boost 私有成员存在跨 DLL 销毁风险。已全部修复。

| 类/接口 | 模块 | STL/Boost 私有成员 | 修复内容 |
|---------|------|-------------------|----------|
| `SyGroup` | Engine2D | `vector<SyEntity*>` / `vector<SyGroup*>` / `string` | 析构声明移至 `.h`，定义 `= default` 移至 `SyGroup.cpp` |
| `EntityContainer` | Engine2D | `vector<unique_ptr<SyEntity>>` / `unordered_map` | 同上，移至 `EntityContainer.cpp` |
| `EntityClipboard` | Engine2D | `vector<unique_ptr<SyEntity>>` | 同上，移至 `EntityClipboard.cpp` |
| `SelectionManager` | Engine2D | `VecSyEntityPtr`（`vector<SyEntity*>`） | 同上，移至 `SelectionManager.cpp` |
| `GroupManager` | Engine2D | `unordered_map<unique_ptr<SyGroup>>` | 同上，移至 `GroupManager.cpp` |
| `SceneNotifier` | Engine2D | `vector<IObserver*>` / `vector<SyEntity*>` | 同上，移至 `SceneNotifier.cpp` |
| `SpatialIndex3D` | Engine3D | `boost::geometry::index::rtree` | 同上，移至 `SpatialIndex3D.cpp` |
| `IUndoCommand3D` | UI3D | 纯虚接口（无数据成员），但 vtable 需在 DLL 内生成 | 析构声明移至 `.h`，定义 `= default` 移至 `UndoRedoManager3D.cpp` |

#### 7.5 其他最终修复（2026-08-02）

| 项目 | 文件 | 问题 | 修复 |
|------|------|------|------|
| `UI3D_GetVersion()` | `UI3DModule.cpp` | 硬编码 `__declspec(dllexport)` | 改用 `UI3D_API` 宏 + 添加 `#include "UI3D/UI3DAPI.h"` |
| `EnginePersistence` | `CMakeLists.txt` | 静态库设置 `ENGINE_EXPORTS` 导出宏（与 Engine Common 共用同名宏产生误导） | 注释移除 `LIB_EXPORT_MACRO` 和 `target_compile_definitions` |

#### 构建验证

- ✅ `Engine2D_d.dll`
- ✅ `Engine3D_d.dll`
- ✅ `UI3D_d.dll`
- ✅ `SanYiCAD.exe`（Debug 全量构建通过）

#### 7.6 析构分离第二轮审计（2026-08-02）

全面扫描所有 `*_API` 导出类/接口，发现额外 20 个仍保留 `= default` 内联析构的导出类型。按风险分两类修复。

**高优先级 — 有 STL/Qt 堆分配私有成员（9 个）**

| 类 | 模块 | 风险成员 | 修复 |
|----|------|----------|------|
| `SceneDocument3D` | UI3D | `QString` x3 / `QStringList` / `QDateTime` | 析构分离至 `SceneDocument3D.cpp` |
| `ViewMenu3D` | UI3D | `QList<QAction*>` x2 | 析构分离至 `ViewMenu3D.cpp` |
| `SolidMenu3D` | UI3D | `QList<QAction*>` | 析构分离至 `SolidMenu3D.cpp` |
| `StatusBar3D` | UI3D | `QString` | 析构分离至 `StatusBar3D.cpp` |
| `StatusBarBase` | UICommon | `QString` x4 | 析构分离至 `StatusBarBase.cpp` |
| `SettingsDialogBase` | UICommon | `vector<TabSlot>` (含 `function`+`unique_ptr`) | 析构分离至 `SettingsDialogBase.cpp` |
| `EntityPropertiesDialogBase` | UICommon | `unique_ptr<IPropertyProvider>` | 析构分离至 `EntityPropertiesDialogBase.cpp` |
| `AboutDialog` | UICommon | `GLInfo` (含 `QString` x4) | 析构分离至 `AboutDialog.cpp` |
| `BaseMenu` | UICommon | `QHash` x4 / `QString` | 析构分离至 `BaseMenu.cpp` |

**中优先级 — 纯虚接口 vtable 一致性（11 个）**

| 接口 | 模块 | 修复方式 |
|------|------|----------|
| `ITransformer` | Engine3D | 新建 `ITransformer.cpp` |
| `INestingJobRunner` | Engine2D | 新建 `INestingJobRunner.cpp` |
| `ISceneContext` | Engine/Common | 更新 `ISceneContext.cpp`（原文件仅有注释） |
| `IEntityEditor` | UI2D | 新建 `IEntityEditor.cpp` |
| `IOperation` | UI2D | 新建 `IOperation.cpp` |
| `ICommandPlugin` | UICommon | 新建 `ICommandPlugin.cpp` |
| `ISettingsRepository` | UICommon | 新建 `ISettingsRepository.cpp` |
| `IMaterialRepository` | UICommon | 新建 `IMaterialRepository.cpp` |
| `IPropertyProvider` | UICommon | 新建 `IPropertyProvider.cpp` |
| `IAlgorithmTaskHandler` | UICommon | 新建 `IAlgorithmTaskHandler.cpp` |
| `LaserStrategy` | Engraving | 追加至 `LaserStrategy.cpp` |

**文档修正**

| 项目 | 修正内容 |
|------|----------|
| `GEOMODELCORE_EXPORTS` → `GEOMODEL_EXPORTS` | 导出宏定义表中引用名与实际代码不一致，已修正 |

**跳过项（无 STL/Qt 数据成员，无需修复）**

`StlLoader`、`ObjLoader`（无数据成员）、`TranslateTransformer`、`ScaleTransformer`、`RotateTransformer`（POD 成员）、`ArrayAlgorithm`、`EntityDiscretizer`、`TestEntityGenerator`（无 STL 数据成员）、`ViewCamera3D`、`CameraController3D`（POD 成员）、`LocalizableDialog`、`ISettingsTab`、`IMenu`（无额外数据成员）、Vision 模块 3 个（沙箱模块）

#### 第二轮构建验证

- ✅ `EngineCommon_d.dll`
- ✅ `Engine2D_d.dll`
- ✅ `Engine3D_d.dll`
- ✅ `UICommon_d.dll`
- ✅ `Engraving_d.dll`
- ✅ `UI2D_d.dll`
- ✅ `UI3D_d.dll`
- ✅ `SanYiCAD.exe`（Debug 全量构建通过）

---

### 8. 改法参考（与已收口模式对齐）

| 泄漏形式 | 推荐改法 |
|----------|----------|
| `std::string` 入参 | `const char*` |
| `std::string` 出参 | `char* buffer, size_t size` |
| `std::vector<T>` 返回 | `forEachXxx(callback, ctx)` 或 `const T*+count` |
| `std::vector<T>` 入参 | `const T*, size_t` |
| `std::unique_ptr<T>` 返回 | 句柄或裸指针 + 对称 destroy |
| `std::shared_ptr<T>` 返回 | 句柄或 `void*` + release |
| 导出类 STL 私有成员 | PIMPL |
| 模板注册/工厂函数 | 逻辑下沉到 cpp，对外非模板 |
| `std::function` 回调 | C 函数指针 + `void* ctx` |
| Qt 信号带 STL 参数 | `const T**, int count` |

---

## 三十二、分批收口记录

> 各批次按时间正序排列（最早在前）。最新批次见「全局状态」。

---

## 批次 D

### Engine/2D 图元族 clone() 收口完成（2026-08-01）

审计发现 `clone()` 已在先前迭代中收口为返回裸指针 `SyEntity*`（非 `std::unique_ptr`），15 个派生类实现均已使用 `new SyXxx(*this)`，STL 成员均已私有化，析构均已分离至 `.cpp`。

#### 本轮新增

| 变更项 | 说明 |
|--------|------|
| `IEntity::destroy()` | 纯虚方法 |
| `SyEntity::destroy()` | 实现 `delete this`（在 DLL 内执行，确保 new/delete 同 CRT，参照规则9 工厂+Release 模式） |
| `SyEntityDeleter` 结构体 | 自定义 deleter |
| `SyEntityOwnPtr` typedef | `unique_ptr<SyEntity, SyEntityDeleter>`，跨 DLL 安全的独占指针 |
| `SmartLineTool.cpp` | local `unique_ptr` 改用 `SyEntityOwnPtr`（消除跨 DLL delete） |
| FileManager/ExportService/DocumentExportAdapter | 4 处 `Fio::VecSyEntityPtr` 约束的调用点添加 ABI 注释（`/MD` 共享堆下安全，如切换 `/MT` 需改用 `SyEntityOwnPtr`） |

> **注意：** Engine2D 内部 77 处 `unique_ptr<SyEntity>(clone())` 保持原样（同 DLL 内 `/MD` 安全）。`cloneEntity()` 调用点：零（死代码路径，仅 `IEntity` 接口形式存在）。

#### 构建验证

- ✅ `EngineCommon.dll`
- ✅ `SanYiCAD.exe`（Release 全量构建通过）

---

---

## 批次 H

### 4 组活跃跨 DLL STL 违规收口 + SceneManager.addEntities C-safe 补齐（2026-08-14）

#### 接口变更

**1) `ImageUtils.h:26`（FileIO）**
| 原接口 | 新接口 | 说明 |
|---|---|---|
| `size_t readImageInfo(const std::string& path, …)` | `size_t readImageInfo(const char* strUtf8Path, …)` | `const char*` 消除 STL 跨 DLL 边界 |

**2) `Engine/Text/FontUtil.h` + `FontManager.h`（Engine2D）**
| 原接口 | 新接口 | 说明 |
|---|---|---|
| `FontData{ std::vector<FT_Byte> vData }` | `FontData{ FT_Byte* pBuffer; FT_Long nBufferSize }` | 裸指针+大小，避免 `vector` 跨 DLL |
| `loadFontFace(FT_Library&, const std::string&)` / `fontFamilyNames(const std::string&)` / `getFontPaths(...)` / `toUtf8/fromUtf8` | header-inline（编译进调用方）/`const char*` | 仅导出指针+长度/裸指针，STL 在 DLL 内部 |
| `FontUtil::compactFamilyKey(const std::string&)` 等静态方法 | header-inline | STL 在 DLL 内部 |

**3) `Plugin/CommandPluginRegistry.h` + API（UI2D）**
| 原接口 | 新接口 | 说明 |
|---|---|---|
| `registerPlugin(std::shared_ptr<ICommandPlugin>)` | `registerPlugin(ICommandPlugin*, void(*)(ICommandPlugin*))` | 裸指针 + 销毁回调 |
| `m_plugins: QList<std::shared_ptr>` | `QList<PluginEntry{ ICommandPlugin* + destroyFn }>` | 不再跨 DLL 传递 `shared_ptr` |

**4) `Engine/2D/Edit/SceneUndoCommands.h`（Engine2D）**
| 原接口 | 新接口 | 说明 |
|---|---|---|
| `EntitySnapshotsCommand/ DeleteEntitiesCommand/ AddEntitiesCommand` 类 `ENGINE2D_API` + public STL ctor | 去除导出；ctor 仅在 DLL 内部使用 | 外部无法跨 DLL `new` |
| `captureEntitySnapshots(...)->vector<unique_ptr>` | 新增 `captureEntitySnapshots(scene, ids, count, out, outCap)->size_t` | 输出裸指针缓冲，所有权转给调用方/工厂 |
| — | 新增 `createEntitySnapshotsCommand/ createDeleteEntitiesCommand/ createAddEntitiesCommand` | 导出工厂，入参为 POD 指针+计数，返回 `IUndoRedoCommand*` |

**5) `SceneManager.h`（Engine2D）额外补齐**
| 原接口 | 新接口 | 说明 |
|---|---|---|
| `addEntities(const VecSyEntityPtr&)` (被 NestingEngine 跨 DLL 调用) | 新增 `addEntities(SyEntity* const*, size_t)` | C-safe，NestingEngine 改用 `.data()/.size()` |

#### 消费方适配

| 文件 | 变更 |
|---|---|
| `FileIO/SyDocumentData.cpp` / `ImageUtils.cpp` | `readImageInfo` 调用加 `.c_str()` |
| `Engine2D/Src/FontManager.cpp` | 去除 inline 化的 FontUtil 方法定义；`loadFontFace`/`.data()` 等适配 |
| `Engine2D/FontUtil.h` | `toUtf8/fromUtf8` 移为 header-inline；`FontData` 成员改裸指针 |
| `UI2D/Src/Font/FontManager.cpp` | 字体注册统一 `.c_str()` |
| `UI/Common/FontLoader.cpp` | `FontManager` API 调用适配 |
| `Engine/Common/Plugin/CommandPluginLoader.cpp` | 改为 `commandPluginRegistryRegister(rawPlugin, destroyFunc)` |
| `UI/2D/Src/Plugin/DemoCommandPlugin2D.h/.cpp` | 提供 `destroyDemoCommandPlugin2D` |
| `UI/2D/Src/Plugin/DemoCommandPlugin3D.h/.cpp` | 同 |
| `UI/2D/Src/Operation/OperationSetup.cpp` / `UI/3D/Src/Plugin/PluginManager3D.cpp` | 注册调用改指针+回调 |
| `UI/2D/Src/Algorithm/AlgorithmTaskRegistration2D.cpp` | `createAddEntitiesCommand` 替代 `make_unique<AddEntitiesCommand>` |
| `UI/2D/Src/Operation/ArrayOperations.cpp` | 同 |
| `Main/Src/UI/Test/UndoRedoRegressionTests.cpp` | 6 处构造改工厂 + C-safe capture |
| `UI/Common/Src/Algorithm/NestingEngine.cpp` | `addEntities(newEntities.data(), newEntities.size())` |

#### 构建验证

- ✅ `Engine2D_d.dll`
- ✅ `UI2D_d.dll` / `UICommon_d.dll`
- ✅ `SanYiCAD.exe` + `MainTests.exe`（Debug 全量构建通过）
- ✅ `UndoRedoRegressionTests` 28/28 全过；`MainTests.exe` 700/700 全过（1 Qt-GUI 跳过）

---

---

## 批次 F

### Hardware loadToolpath 收口 + 文档校正（2026-08-01）

#### 接口变更

| 原接口 | 新接口 | 说明 |
|--------|--------|------|
| `const std::vector<HardwareToolpathPoint>&` | `const HardwareToolpathPoint*, size_t count` | pointer+count，消除 STL 跨 DLL 边界 |

涉及：
- `ToolpathExecutor::loadToolpath`
- `LaserController::loadToolpath`

#### 其他变更

- `ToolpathExecutor` 析构函数声明移至 `.h`、定义移至 `.cpp`（模式5，确保 `m_points` 的 STL 释放在 DLL 内）
- `LaserController.h` 移除 `#include <vector>`
- 调用方 `UI2D/HardwareLaserService.cpp` 适配 `.data()/.size()`
- `Network/RetryPolicy.h` 补内部 ABI 标注注释（当前孤立未被跨 DLL 消费）

#### 文档校正

| 项目 | 状态变更 | 说明 |
|------|----------|------|
| `GeoModelDLL.h` 版本查询接口 | 🟡 → ✅ | `GeoModel_GetVersion()` / `GeoModel_GetVersionString()` 实际已存在 |
| `FileIOExport.h` C API 层 | 🟡 → ✅ | 已删除，异常安全问题随 C API 删除消除 |

#### 构建验证

- ✅ `Hardware.dll`
- ✅ `UI2D.dll`
- ✅ `SanYiCAD.exe`（Release 全量构建通过）

---

---

## 批次 C C1

### UndoRedoManager3D 收口完成（2026-07-31）

#### 核心变更

- `UndoRedoManager3D.h` **PIMPL 化**（`Impl*`，命令栈 `std::vector<std::unique_ptr<IUndoCommand3D>>` 与 `m_sceneManager` 全部下沉 `.cpp`）
- 析构已声明并定义，消除 `Main.exe` 构造/析构与 `UI3D.dll` 之间的 CRT 分配/释放错配风险
- `pushCommand(std::unique_ptr<IUndoCommand3D>)` → `pushCommand(IUndoCommand3D*)`（接管所有权，调用方 `.release()`）
- `IUndoCommand3D::description()` → `const char*`（三个命令实现随改，`SceneReplaceCommand::m_description` 改 `std::string`），头文件不再依赖 STL

#### 消费方适配

| 文件 | 变更 |
|------|------|
| `SceneEditService3D.cpp` | `makeDelete` / `makeTransform` → `.release()` |
| `FileOperations3D.cpp` | `makeSceneReplace` → `.release()` |
| `ModelOperations3D.cpp` | `makeSceneReplace` → `.release()` |
| `ModelSplit3D.cpp` | `makeSceneReplace` → `.release()` |

#### 构建验证

- ✅ `UI3D.dll`
- ✅ `UI3DStorageTests`（`SceneDocumentIO3DTest` 全过）
- ✅ `SanYiCAD.exe`
- ✅ `ImportExport 87/87` + `FrameworkRegression 17/17`

#### 测试修复

- `Scene3DRegressionTests.cpp`：4 个 `SceneManager3D` 测试构造空网格不满足 `addEntity` 的 `isValid()`（≥3 顶点）约束导致必然失败，补 `makeTriangleMesh` 帮助函数。

#### 已知既有问题（与本批次无关，未处理）

- `MainTests` 全量运行在 `RenderViewport2DRegressionTest.InputRouter_EventFilterNonRenderWidget` 处崩溃（`0xC0000409`）
- `RenderViewport2D` 5 个测试失败（`Camera2D` 矩阵 / `PefMonitor` / 选择链）

---

---

## C3：SySerializer blob 化

### C 批 FileIO 收口 C3（2026-07-31）

#### SerializeResult 改纯 POD

```cpp
struct SerializeResult {
    bool success;
    char errorMessage[512];
    // ok() / fail(const char*)
    // fail 用 strncpy 截断
};
```

#### 接口变更

| 接口 | 变更 |
|------|------|
| `serializeToMemory` | 第二参数改 `BinaryBlobOut*`（两段式：`data=nullptr` 仅查询大小、`size` 写回 `written`；**必须传指针**，按值传时 `written` 无法回传） |
| `deserializeFromMemory` | 改 `BinaryBlob` 入参（空数据为合法空 protobuf → success） |
| 文件路径 | 统一 `const char*` |
| 警告 | 改 C 函数指针回调：`typedef void (*SerializeWarningCallback)(const char*, void*)` |
| `setCryptoProvider` | 保留裸指针 + PIMPL 隐藏实现 |

> 头文件不再依赖 STL（补 `<cstring>` 供 `strncpy`）。

#### 消费方适配

| 文件 | 变更 |
|------|------|
| `FileManager.cpp` | `.toUtf8().constData()` 传路径、`loadResult.errorMessage` 直接用 `char[]`、警告收集改回调 |
| `NativeParser.cpp` | 去 `result.warnings` |
| `NativeParser3D.h/.cpp` | `parseDocument(const std::string&)` → `const char*`；警告经 `WarningCollector` + `collectWarning` 回调收集回 `ParseResult` |
| `SySerializerTests.cpp` | 整体重写：`serializeDoc` / `deserializeDoc` 帮助函数 + `BinaryBlobOut` 指针两段式 |
| `FileIORegressionTests.cpp` | 适配新接口 |
| `FrameworkLifecycleTests.cpp` | 新增帮助函数 |

#### 构建验证

- ✅ `FileIO.dll`
- ✅ `FileIOTests 118/118`
- ✅ `MainTests` 编译链接
- ✅ `SanYiCAD` 全量构建
- ✅ `ImportExport 87/87`

#### 此前 UI/3D 既有错误修复

| 文件 | 修复内容 |
|------|----------|
| `BRepModelService3D` | `saveStepFile` → `c_str` |
| `ModelSplit3D` | `const` + `move` 拷贝 / `SelectionManager3D` include / 拾取回调改 C 函数指针 |
| `ModelOperations3D` | 三处 `std::move(before)` |
| `ReliefEngravingParams3D` | `toUtf8` |
| `ReliefToolpathPreview3D` | `emplace_back` 三参非法 |
| `FileManager.cpp` | `result.warnings` 已随 C3 回调化消除 |

> 另：`BUILD_RENDERX=ON`（`SanYiRender` 缺失曾阻断 UI3D/Main 链接，现已构建）；`FileIOError.h` 已删除。

#### C3 剩余待办

- `SyDocument.h` / `SyDocument3D.h` PIMPL / 固定缓冲收口

---

---

## C3 收口补全：SyDocument/SyDocument3D 全 PIMPL

### 完成日期：2026-07-31

#### SyDocument.h

- 改为**不透明类**（`SyDocumentData*` 私有指针 + `friend` 内部访问器）
- 原公开 STL 结构体（`DocumentMetadata` / `LayerInfo` / `HardwareInfo` / `GroupInfo` / `PropertyMap`）及全部容器成员内迁至 `Src/Internal/SyDocumentData.h`
- 元数据改 `const char*` getter/setter
- 图层改 POD 视图 `SyLayerInfo`
- 硬件改 POD 视图 `SyHardwareInfo`
- 图元改借用指针（`entityAt` / `addEntity`，`addEntity` 接管所有权）
- `SyDocumentPtr` 已删除

#### SyDocument3D.h

- 同样 PIMPL 且公开头不再 include `Engine3D`

#### 消费方适配

| 类型 | 文件 | 变更 |
|------|------|------|
| 内部 | `SySerializer` / `NativeParser(3D)` | 移出图元，经 `syDocumentData(doc)` 访问 |
| 外部 | UI `FileManager`（读/写 `.sy`） | 改用公开访问器 |
| 死代码 | `SceneManager::saveGroups` / `loadGroups` | 已删除并移除 `FileIO/SyDocument.h` 依赖 |
| 测试 | `SySerializerTests` / `FileIORegressionTests` / `FrameworkLifecycleTests` | 全部改为访问器 / POD 视图 |

#### 构建验证

- ✅ `FileIO.dll`
- ✅ `FileIOTests 118/118`
- ✅ `MainTests ImportExport 87/87` + `FrameworkRegression 17/17`
- ✅ 全量 `ALL_BUILD`
- ✅ `SanYiCAD.exe`

---

---

## C2：工厂收口

### C 批 FileIO 收口 C2：FileParserFactory / FileWriterFactory（2026-07-31）

#### 工厂 PIMPL 化

- `Impl*` 隐藏 `std::map` / `std::function` / `std::vector` 成员
- `CreatorFunc` 由 `std::function<std::unique_ptr<T>()>` 改为 C 函数指针：
  - `IFileParser* (*)()`
  - `IFileWriter* (*)()`

#### 接口变更

| 原接口 | 新接口 |
|--------|--------|
| `createParser` / `createParserByExtension` / `createWriter` | 返回裸指针（所有权转移）+ 新增 `destroyParser` / `destroyWriter` |
| `supportedFormats` / `allSupportedExtensions` / `supportedExtensions`（返回 `vector`） | `forEachSupportedExtension(callback, ctx)` 回调遍历 |
| 字符串参数 | `const char*` |

> 头文件不再 include `IFileParser` / `IFileWriter` / STL（仅前置声明）。

#### 消费点适配

| 文件 | 变更 |
|------|------|
| `FileIOManager.cpp` | 裸指针 + `try/catch` `destroyParser` / `destroyWriter` 防泄漏；扩展名列表改回调收集后 join |
| `FileImporter.cpp` | `detectFormat(ext.c_str())`、裸指针 + `destroyParser` |

#### 构建验证

- ✅ `FileIO` 编译通过
- ✅ `FileIOTests 119/119`
- ✅ `MainTests ImportExport 87/87` 全部通过
  - 此前唯一失败 `FioEntityConverter_NameTransfer` 为测试断言过期，已按实现修正（`convertEntity` 创建时即 `setName`）
- ✅ `SanYiCAD` 全量构建通过

---

---

## C1：FileIOManager 接口收口

### C 批 FileIO 收口 C1（2026-07-31）

#### FileIOManager.h 重写为 ABI 安全 facade

- 回调改 C 函数指针 + `void* ctx`：
  - `ImportCallback` / `ExportCallback` / `WarningCallback`
  - `setImportCallback` / `setExportCallback`

#### 接口签名变更

| 方法 | 新签名 |
|------|--------|
| `importFile` | `const char*` + `Eg::SyEntity***` + `size_t*` + `char* errorBuffer`（重载含警告回调与 `size_t* outLayerCount`） |
| `exportFile` | `const char*` + `const Eg::SyEntity* const*` + `size_t` + `errorBuffer` |

- 新增静态 `deleteEntities` / `freeEntityArray`
- 新增查询：`supportedImportExtensions` / `supportedExportExtensions` / `canImport` / `canExport`

> 头文件仅前置声明 `Eg::SyEntity`，不再 include `IFileParser` / `IFileWriter` / `FileIOError` / STL。

#### 调用方适配

| 模块 | 变更 |
|------|------|
| Main 5 个 ImportReader（Dxf/Svg/Plt/Pdf/Step） | 经裸指针数组 + `freeEntityArray` 转移进 `VecSyEntityPtr`（Dxf 经 `outLayerCount` 恢复图层数） |
| 6 个 ExportWriter | 构造裸指针数组调用 |
| UI2D `FileManager` | import / export 两处调用同步改造 |
| `FileImporter.cpp` | 内部改 `FileParserFactory` 直连 |

#### 构建验证

- ✅ `FileIO` 编译通过
- ✅ `FileIOTests 119/119`
- ✅ `MainTests ImportExport 86/87`（唯一失败 `FioEntityConverter_NameTransfer` 为既有问题，与 C1 无关）
- ✅ `SanYiCAD` 全量构建通过

---

---

## 第四批

### 第四批 ABI 收口：Log + SettingsTable

#### SettingsTable.h 全接口收口（Top1）

- **PIMPL** 隐藏 `std::map` / `std::variant`（`SettingsTableImpl*`）
- 导出方法全改 `const char*` / 缓冲区（含 `getString(char*, size)` buffer 版）
- 移除 `setValue` / `getValue`（variant）与 `values()`（map）
- 新增 ABI 安全 `forEach(callback, ctx)` 遍历
- `SettingsRepositoryImpl::saveTable` 改回调方式写库
- Header 内联便捷包装（`getString(const char*, const char*)`、`setString(const char*, const std::string&)`）保持调用方零改动

#### Log 模块收口

- `SyTraceContext` 导出函数改 `const char*` / 调用方缓冲区：
  - `currentTraceId(char*, size)`
  - `resolveTraceId(const char*, char*, size)`
- `currentTraceIdString` / `resolveTraceIdString` 为 header 内联便捷包装（编译进调用方，不跨 DLL 边界）
- `SyLogger` 删除 `Initialize(const std::string&)` 导出重载
- `LogPathCallback` 由 `std::function<std::string()>` 改为 `const char* (*)(void* ctx)` + `SetLogPathCallback(cb, ctx)`

#### 调用方适配

`OperationTrace`、`HttpRequest`、`UploadManager`、`DownloadManager`、`PythonHost`、`TaskExecutor`、`FaceRecognitionProcessor`、`AppInitializer thunk`

#### 测试

- 新增 `SettingsTableTest` 10 用例
- 新增 `SyTraceContextTest` 4 用例

#### 构建验证

- ✅ `Log` / `PythonHost` / `UICommon` / `UI2D` / `UI3D` / `SanYiCAD` 编译通过
- ✅ `LogTests` 编译 + 运行通过
- ✅ `SettingsTableTest` 运行通过
- ✅ `UICommonCommandKernelTests` 编译通过

---

---

## 第三批

### 第三批 ABI 收口完成（含 UI2D 调用方适配与全量编译通过）

#### 补齐 IEntityEditor / EntityEditorFactory 收口后的调用方适配

- `EntityEditManager` 回调桥接 C 函数指针 + `void* ctx`
- `ToolInitializer` 移除旧的重复工厂实现并改裸指针注册
- `EntityEditorFactory.cpp` 修 `SyEntity` 不完全类型与 `eType` 访问
- `SplineEditor` 交互回调补 `ctx`

#### 顺手修复

- `SceneManager3D::selectMesh` 残留调用（`UiEntities.cpp` / `SimpleRenderer3D.cpp` 改用 `selectEntity(IEntity*)`）

#### 构建验证

- ✅ `SanYiCAD` 全量构建通过

---

---

## 第二批

### 第二批 ABI 收口完成

| 接口 | 变更 |
|------|------|
| `BRepModelService3D::tessellateToEntity` | `const char*` + `errorBuffer` |
| `BRepModelService3D::saveSceneAsStep` | `const char*` + `errorBuffer` |
| `GmcMeshBridge3D` 全部方法 | `const char*` |
| `ToolManager3D::registerTool` | 模板下沉为非模板 `FactoryFn` 版本 |

#### 调用方适配

`FileOperations3D` / `ModelOperations3D` / `ModelSplit3D`

#### 测试更新

- `ToolManager3DTests`

---

---

## 第一批

### 第一批 ABI 收口完成

- `RenderWidget3D` 信号 / 回调 / 结构体全部 POD 化
- `SceneEditService3D::commitTransformUndo` 改为 POD 扁平数组参数
- `UndoCommands3D::makeTransform` 改为 POD 扁平数组参数
- 新增第十四节「UI2D / UI3D ABI 分批收口清单（实施计划）」

---

---

## P0 / P1 修复

### 第三批内容回顾（P0 修复）

| 项目 | 变更 |
|------|------|
| `SceneBuilderBase.h` 纯虚接口 | `std::shared_ptr` → `SceneDocumentBase*` + 新增 `destroyScene` |
| `LaserToolpathBridge::executeLaserToolpath` | 改 `const LaserToolpathPoint* + count` 替代 `std::vector` |
| `BRepModelService3D` | PIMPL 化私有 `std::unordered_map` 成员（#11） |
| `ToolManager3D` | PIMPL 化私有 `std::vector` / `std::unordered_map` 成员（#13） |

### P1 修复轮次

- `EngineAPI` 拆分确认

---

---

## 文档整合

`DLL-ABI复查报告-跨DLL-STL违规清单-2026-07-31.md` 已并入本文档（见「二十五、跨 DLL STL 违规清单（全模块详细清单）」），原独立文档已删除。

> **后续所有 DLL / ABI 相关改动只维护本文档一个入口。**

---

*报告生成时间：2026-08-01*

---

---

## 三十三、UI2D / UI3D ABI 分批收口清单（实施计划）

> 本节是基于代码扫描生成的**分批收口优先级清单**，可直接作为开发任务清单使用。
> 扫描范围：`UI/2D/Include/`、`UI/3D/Include/` 下所有 `.h` 文件。
> 统计方式：`Select-String -Pattern 'std::string|std::vector|std::unique_ptr|std::shared_ptr|std::map|std::unordered_map'`

### 总览

| 模块 | STL 泄漏文件数 | 最高命中次数 |
|------|--------------|------------|
| UI3D | 14 | 14（`BRepModelService3D.h`） |
| UI2D | 11 | 3（`IEntityEditor.h` / `EntityEditorFactory.h`） |
| 合计 | 25 个头文件 | — |

### 第一批：最危险 — 虚函数 / 信号 / 回调签名里直接带 STL（跨 DLL 调用必崩）

> 这批是**必须最先改的**，因为它们已经出现在跨 DLL 信号和回调链路里。
> 改完之后，3D 编辑 / 变换 / 撤销链路的跨 DLL 崩溃风险会直接下降一大截。

| # | 文件 | 泄漏点 | 风险说明 | 建议改法 | 状态 |
|---|------|--------|----------|----------|------|
| 1 | `UI3D/Render3D/RenderWidget3D.h` | `sigSelectionChanged(const std::vector<Eg::SyMeshEntity*>&)` | 信号跨 DLL 时 `std::vector` 布局不兼容 | 改为 `sigSelectionChanged(const Eg::SyMeshEntity**, int)` | ✅ 已修复 |
| 2 | `UI3D/Render3D/RenderWidget3D.h` | `setToolpathOverlay(const std::vector<RenderToolpathOverlayLine>&)` | 入参带 STL 容器 | 改为 `const RenderToolpathOverlayLine*, int` | ✅ 已修复 |
| 3 | `UI3D/Render3D/RenderWidget3D.h` | `TransformUndoHandler` 回调签名带 5 层 `std::vector` 嵌套 | 回调跨 DLL 时必然崩 | 改为 C 风格函数指针 + POD 扁平数组 + 上下文指针 | ✅ 已修复 |
| 4 | `UI3D/Edit/SceneEditService3D.h` | `commitTransformUndo()` 同样 5 层 STL 嵌套 | 同上 | 改为 POD 扁平数组参数（同 makeTransform 签名） | ✅ 已修复 |
| 5 | `UI3D/Edit/UndoCommands3D.h` | `makeTransform` 工厂函数带 `std::vector<std::vector<...>>` 嵌套 | 嵌套 STL 跨 DLL 传递 | 改为 POD 扁平数组参数；makeDelete/makeSceneReplace 同 DLL 内部保留 | ✅ 已修复 |

### 第二批：高危 — 导出类公共方法带 STL 返回 / 入参

> 这批特点是：调用方和被调用方可能不在同一 DLL，返回值或入参直接带 STL。

| # | 文件 | 泄漏点 | 风险说明 | 建议改法 | 状态 |
|---|------|--------|----------|----------|------|
| 6 | `UI3D/Service/BRepModelService3D.h` | `tessellateToEntity(..., const std::string& name)` 返回 `std::unique_ptr` | 返回值跨 DLL | 改 `const char*` 入参；返回用句柄或 sink | ✅ 已修复（const char* 入参，unique_ptr 同 DLL 内） |
| 7 | `UI3D/Service/BRepModelService3D.h` | `saveSceneAsStep(..., const std::string& filePath, std::string* errorMessage)` | 跨 DLL 传 `std::string*` | 改 `const char*` / `char* buffer, size_t` | ✅ 已修复（const char* + errorBuffer/size） |
| 8 | `UI3D/Service/BRepModelService3D.h` | `attachBRep` / `getBRep` 返回 `std::shared_ptr<TopoShape>` | 所有权跨 DLL | 改为句柄或 `void*` + 配套 release | ✅ 已标注（同 DLL 内部，不跨 DLL ABI 承诺） |
| 9 | `UI3D/Adapter/GmcMeshBridge3D.h` | `toSyMeshEntity / tessellateToEntity` 入参 `std::string`，返回 `std::unique_ptr` | 同上 | 同上 | ✅ 已修复（const char* 入参） |
| 10 | `UI3D/Manager/ToolManager3D.h` | `registerTool` 模板 + `m_factories` 用 `std::shared_ptr` / `std::vector` | 模板展开在调用方，ABI 不稳 | 模板逻辑下沉到 cpp，对外只暴露非模板注册接口 | ✅ 已修复（FactoryFn 非模板版本） |

### 第三批：中危 — 内部使用为主，但头文件暴露 STL 成员

> 这批可以靠 PIMPL + 头文件瘦身一次性解决，不需要改接口语义。

| # | 文件 | 泄漏点 | 风险说明 | 建议改法 | 状态 |
|---|------|--------|----------|----------|------|
| 11 | `UI3D/Service/BRepModelService3D.h` | 私有成员 `std::unordered_map<..., std::shared_ptr<...>>` 两份 | 头文件被 include 时 STL 泄漏到下游编译单元 | 用 PIMPL 把私有成员下沉 | ✅ 已修复（2026-07-31 PIMPL 化，Impl 下沉到 .cpp） |
| 12 | `UI3D/Render3D/RenderWidget3D.h` | `RenderToolpathOverlayLine::points` 是 `std::vector<float>` | 结构体成员跨 DLL | 改为 `const float*, int` | ✅ 已修复 |
| 13 | `UI3D/Manager/ToolManager3D.h` | 私有成员 `std::vector` / `std::unordered_map` | 同上 | PIMPL | ✅ 已修复（2026-07-31 PIMPL 化，Impl 下沉到 .cpp） |
| 14 | `UI2D/Option/EntityEdit/IEntityEditor.h` | 回调 + ControlPoint + EditorUtils 中 STL | 编辑器接口跨 DLL | 已标注为 UI2D 内部编辑器抽象 | ✅ 已标注（内部 Qt 体系，不跨 DLL ABI 承诺） |
| 15 | `UI2D/Option/EntityEdit/EntityEditorFactory.h` | EditorCreator + createEditor + 私有成员 | 同上 | 函数指针 + 裸指针 + PIMPL | ✅ 已修复（EditorCreator 为函数指针，createEditor 返裸指针，PIMPL 下沉） |
| 16 | `UI2D/Service/HardwareLaserService.h` | 2 处 STL | 服务接口 | 已标注为 UI2D 内部硬件适配层 | ✅ 已标注（内部使用，不跨 DLL ABI 承诺） |

### 第四批：低危 — 头文件里有 STL，但实际不跨 DLL，或已 PRIVATE include 保护

> 这批可以留到最后，或者干脆只在文档里标注"同模块内部使用，不跨 DLL"。

| # | 文件 | 泄漏点 | 风险说明 | 建议改法 | 状态 |
|---|------|--------|----------|----------|------|
| 17 | `UI2D/Operation/OperationRegistry.h` | 1 处 STL | 内部注册表 | 可保留，PIMPL 更佳 | ⚠️ 可保留 |
| 18 | `UI3D/Operation/OperationRegistry3D.h` | 1 处 STL | 同上 | 同上 | ⚠️ 可保留 |
| 19 | `UI3D/Operation/OperationBus3D.h` | 1 处 STL | 同上 | 同上 | ⚠️ 可保留 |
| 20 | `UI3D/Relief/ReliefEngravingJob3D.h` | 1 处 STL | 内部任务 | 可保留 | ⚠️ 可保留 |
| 21 | ~~`UI2D/CommandManager.h`~~ | ~~1 处 STL~~ | ~~内部管理器~~ | ~~可保留~~ | ✅ 已删除（2026-08-14 旧操作层清理） |
| 22 | `UI2D/Option/EntityEdit/BaseEditor.h` | 1 处 STL | 基类内部 | 可保留 | ⚠️ 可保留 |
| 23 | `UI2D/Service/ILaserService.h` | 1 处 STL | 接口 | 可保留或改 `const char*` | ⚠️ 可保留 |
| 24 | `UI2D/Service/SceneMonitor.h` | 1 处 STL | 内部监控 | 可保留 | ⚠️ 可保留 |
| 25 | `UI3D/Operation/UiStateBridge3D.h` | 1 处 STL | 内部桥接 | 可保留 | ⚠️ 可保留 |

### 建议执行节奏

| 批次 | 内容 | 建议时间 | 预期效果 |
|------|------|----------|----------|
| **第一批** | `RenderWidget3D` 信号/回调 + `SceneEditService3D::commitTransformUndo` + `UndoCommands3D` 工厂 | ✅ 已完成 | 3D 编辑/变换/撤销链路跨 DLL 崩溃风险直接下降 |
| **第二批** | `BRepModelService3D` + `GmcMeshBridge3D` + `ToolManager3D` 模板注册 | ✅ 已完成 | B-Rep / 网格 / 工具注册链路收口 |
| **第三批** | PIMPL 化私有成员 + 结构体成员 POD 化 + 编辑器接口 `const char*` 化 | ✅ 已完成 | BRepModelService3D/ToolManager3D PIMPL（#11, #13）；IEntityEditor/EntityEditorFactory 接口收口（#14, #15）及调用方适配（EntityEditManager / ToolInitializer / SplineEditor）；SceneBuilderBase P0 修复；LaserToolpathBridge P0 修复 |
| **第四批** | 内部注册表 / 命令管理器 | 最后或文档标注即可 | 无需改动，仅需标注"同 DLL 内部使用" |

### 改法参考（与本文档已有模式对齐）

| 泄漏形式 | 推荐改法 | 对应模式 |
|----------|----------|----------|
| `std::string` 入参 | `const char*` | 模式 A |
| `std::string` 出参 | `char* buffer, size_t size` | 模式 A |
| `std::vector<T>` 返回 | `forEachXxx(callback, ctx)` | 模式 C |
| `std::vector<T>` 入参 | `const T*, size_t` | POD 数组 |
| `std::vector<std::vector<T>>` 嵌套入参 | 展平为 `const T* flatData, const int* counts` + entityCount | 模式 F（嵌套展平） |
| `std::unique_ptr<T>` 返回 | 句柄或 sink 接口 | 模式 D |
| `std::shared_ptr<T>` 返回 | 句柄或 `void*` + release | 模式 D |
| 导出类含 STL 私有成员 | PIMPL（裸指针或析构分离） | 模式 5 / 模式 6 |
| 模板注册函数 | 逻辑下沉到 cpp，对外非模板 | — |
| `std::function` 回调 | C 风格函数指针 + `void* ctx` | 模式 G |
| Qt 信号带 STL 参数 | 改为 `const T**, int count` | 模式 H |

---

---

## 三十四、落地检查清单

```
✅ 标出所有 ABI 边界（已存在的 C API facade、纯虚接口、导出函数）
✅ 区分"必须修复"和"内部可接受"两类 STL 使用
⚠️ 对外接口统一采用 const char* / POD / 回调 / BinaryBlob（P0 已完成，P1 进行中）
⚠️ 检查现有 C API 的句柄风格、错误码、导出宏是否一致（部分不一致）
✅ 修复 Log.dll、GeoModelCore 中已确认的跨边界 STL 问题
✅ Engine 导出宏拆分（ENGINE_API / ENGINE2D_API / ENGINE3D_API 已通过独立文件完成）
✅ 导出类析构函数分离到 .cpp（IUndoRedoCommand, ISceneManager, SyEntity, TextItem, SyLayer, SySmartLine, TextConverter, UndoRedoManager::Command 等）
✅ Engine/2D 图元族 clone() 收口（裸指针 + destroy() + SyEntityOwnPtr 跨 DLL 安全释放）
✅ TopoShape PIMPL 安全（析构已分离至 .cpp）
✅ UI2D/UI3D Src/ 改为 PRIVATE include
✅ UI3D 第一批 ABI 收口（RenderWidget3D 信号/回调/结构体 POD 化）
✅ UI3D 第二批 ABI 收口（BRepModelService3D / GmcMeshBridge3D const char* 化 + ToolManager3D 模板下沉）
✅ UI3D 第三批 ABI 收口（BRepModelService3D / ToolManager3D PIMPL；SceneBuilderBase P0 修复；LaserToolpathBridge P0 修复；IEntityEditor / EntityEditorFactory 接口收口及调用方适配完成，SanYiCAD 全量构建通过）
✅ 统一 facade 的版本查询接口（Log/Nesting/Vision/GeoModelCore/Engraving 已补齐）
❌ 每个导出函数增加参数校验与异常屏蔽（部分已做，大部分未做）
⚠️ 明确线程与回调约定（Nesting、CrashHandler 已较完整）
❌ 依赖检查纳入发布流程
❌ 结构体带 static_assert 大小校验
```

---

---

## 三十五、当前工程的模块迁移表（按"必须修复 / 可接受但要标注"重排）

> 这张表用于把审计结论落实到工程行动上。

| 优先级 | 模块/边界 | 现状判断 | 当前状态 | 建议动作 | 说明 |
|---|---|---|---|---|---|
| P0 | `EngineCommon` 纯虚接口 | 必须修复 | ✅ **已修复** | 改为 `const char*`、回调、迭代器或 POD | `ISceneManager`、`IEntity`、`IUndoRedoCommand`、`SceneRenderContract`、`SyEntity` 均已收口 |
| P0 | `Engine2D` 虚接口 / 导出函数 | 必须修复 | ✅ **已修复** | 改为 POD 或收口到 .cpp 内部实现 | `IUndoRedoManager`、`ILayerManager`、`UndoRedoManager::Command` 均已收口 |
| P0 | `FileIO` 公开接口 | 必须修复 | ✅ C API 已建 + C++ 接口已收口 | C API facade 继续收口，C++ 头文件仅内部使用 | `IFileParser`、`IFileWriter`、`SyCryptoProvider` 均已收口 |
| P0 | `UICommon` 纯虚接口 | 必须修复 | ✅ **已修复** | 重构为 KvPair 回调模式 | `IDatabase`（KvPair + QueryRowCallback）、`IBusinessDataRepository`、`SceneDocumentBase`、`SceneBuilderBase` 均已收口 |
| P1 | `GeoModelCore` 导出函数 | 必须修复 | ✅ **已收口** | `const char*` + POD / Blob；TopoShape PIMPL 已安全 | `GmcStatus` ✅；`TopoShape::dumpStl` ✅ 改为 `const char*`；`makeFillet`/`makeChamfer` ✅ 改为 `const double*, size_t`；`getAllFaces`/`getAllEdges`/`getAllVertices` ✅ 改为 `forEachFace/forEachEdge/forEachVertex` 回调；`GmcBooleanResult`/`GmcSplitResult` `errorMessage` ✅ 改 `char[256]`；`GmcMesh::exportStl` ✅ 改 `const char*` |
| P1 | `Engine3D` public 类 | 可接受但需标注 | ✅ **已标注** | 保持内部 C++ DLL，禁止当作外部稳定 ABI | `SceneManager3D`、`SyMeshEntity`、`Geo3DQuery` 等 |
| P1 | `Engraving` 虚接口 / 类型别名 | 必须修复 | ✅ **已收口** | 对外改成 POD / Blob | `LaserStrategy::getName/adjustParams` ✅ 已改为 `const char*`；`EngravingImageAPI` ✅ 已改为 `const char*`；`Toolpath` / `ToolpathList` 已标注为内部算法层类型 |
| P1 | `UI3D` / `UI2D` 对外导出函数 | 必须修复 | ✅ **第三批已完成** | 收口到 C ABI 或内部封装；`Src/` 已改为 PRIVATE | 大量导出函数含 `std::vector`/`std::string`/`std::unique_ptr` 需分批收口；第一批（RenderWidget3D + SceneEditService3D + UndoCommands3D）✅；第二批（BRepModelService3D + GmcMeshBridge3D + ToolManager3D）✅；第三批（PIMPL / POD / 内部边界注释）✅ |
| P2 | `Hardware` / `PythonHost` / `Log` | 可接受但需标注 | ✅ Log 已修复；✅ Hardware `loadToolpath` 已收口（pointer+count） | 维持内部 C++ DLL 定位；Hardware 的 QString/QStringList 等 Qt 类型跨边界待决策 | 不作为跨编译器 ABI 承诺 |
| P2 | `Utility` 别名 / 小工具 | 可接受但需标注 | ✅ 内部使用 | 保留，但不要扩展为稳定 ABI | 仅作内部基础库 |
| P2 | `SceneDocumentBase` / `SceneBuilderBase` 接口过薄 | 已收口 | ✅ **已修复** | `SceneDocumentBase` 已改为回调枚举（`forEachEntityId`）；`SceneBuilderBase` 已改为 buffer 返回（`defaultRootName`） | 头文件已无 STL 跨边界；不再作为下一步 P2 任务 |
| P2 | `SceneManager` / `SceneManager3D` 双重 API | 部分收口 | ✅ **已部分收口** | 已移除 `SceneManager::selectEntity(SyEntity*)` 和 `SceneManager3D::selectMesh/selectMeshes` 这类同语义具体重载；`ISceneManager` 仍是统一入口，具体类型接口继续保留为内部能力 | 后续继续清理同语义重载与可外提的只读/选择接口，逐步减少“双层面”歧义 |
| P3 | `Vision` | 沙箱评估 | ✅ 已标注 | 视为独立模块或加 C 包装层 | OpenCV + STL 依赖较重 |
| P3 | `UI2D / UI3D Src/` 暴露 | 必须修复 | ✅ **已修复** | `Src/` 已改为 `PRIVATE` | 防止下游绕过 ABI 边界 |

---

---

## 三十六、P0 必修复项的改造路线图

> 本节将审计结论进一步落到可执行方案上，优先覆盖 `EngineCommon`、`FileIO`、`UICommon` 三个必须修复项。

### 1. 总原则

1. **跨 DLL public 接口不再直接暴露 STL。**
2. **内部实现允许继续使用 STL，但必须留在 DLL 内部。**
3. **对外统一采用 `const char*`、POD、回调、`BinaryBlob`、不透明句柄。**
4. **先改接口，再改实现，最后补测试。**

### 2. 哪些 DLL 需要导出 C，哪些只导出 C++ 即可

> 结论：**不是所有 DLL 都必须导出 C。**
> 
> 只有“需要被外部语言、插件、不同编译器、不同 CRT 直接调用”的模块，才必须提供 C ABI facade。
> 仅用于工程内部、并且调用方与被调用方保持同一编译器 / 同一 CRT / 同一构建体系的 DLL，**只导出 C++ 也可以**，前提是把 ABI 风险当作内部约束管理，而不是对外承诺。
>
> 进一步说：**如果一个模块只是内部引擎层、算法层、渲染层，且不打算提供外部插件 API，那么保留 C++ 导出是合理的。** 但前提是 public 头文件不能继续把 STL 当成跨 DLL 稳定接口来用。

#### 2.1 建议必须提供 C ABI facade 的模块

| 模块 | 原因 | 建议 |
|---|---|---|
| `FileIO` | 文件解析 / 写入 / 序列化常被外部工具、脚本、插件复用 | 必须保留 C ABI facade |
| `UICommon` | 业务 / 数据仓库 / 场景文档常被上层模块共享 | 仅内部使用 |
| `License` | 典型跨语言调用边界 | 维持 / 强化 C ABI |
| `CrashHandler` | 崩溃回调、日志收集跨语言风险高 | 维持 / 强化 C ABI |
| `Network` | 如果作为服务边界或外部通信层 | 推荐提供 C ABI |
| `RenderNext`→`SanYiRender` | 作为对外 facade 或兼容层 | 保持纯 C ABI |
| `Nesting` / `Vision`（若对外发布） | 若要给脚本或外部插件调用 | 需要 C ABI 包装层 |

#### 2.2 可以只导出 C++ 的模块

| 模块 | 结论 | 原因 |
|---|---|---|
| `EngineCommon` | 可以只导出 C++，但必须收口 public STL | 主要服务内部引擎体系，不建议直接给外部语言调用 |
| `Engine2D` | 可以只导出 C++ | 内部绘图 / 选择 / 编辑链路，适合内部 ABI 管理 |
| `Engine3D` | 可以只导出 C++ | 场景管理偏内部实现，建议保持内部 DLL 定位 |
| `GeoModelCore` | 可以只导出 C++ | 若只是内部几何内核，可只导出 C++；若要给外部工具，用 facade |
| `Engraving` | 多数情况下可只导出 C++ | 作为内部算法 / 工艺模块更合适 |
| `Hardware` | 可以只导出 C++ | 通常在同一套程序链路内使用 |
| `PythonHost` | 通常可以只导出 C++ | 更像宿主层 / 运行时层，不一定需要 C ABI |
| `Utility` | 可以只导出 C++ | 基础工具库，更多是内部依赖 |
| `Log` | 可以只导出 C++ 或保留少量 C 接口 | 只要接口不跨边界暴露 STL 即可 |

#### 2.3 判断规则

1. **要给外部语言用**：优先 C ABI。
2. **要给插件系统用**：优先 C ABI 或纯句柄方式。
3. **只给同工程 DLL 互调**：可以只导出 C++，但 public 接口必须规避 STL 跨边界。
4. **如果未来可能对外开放**：最好一开始就预留 facade，不要把 C++ ABI 当长期承诺。

#### 2.4 模块具体建议

| 模块 | 建议 | 说明 |
|---|---|---|
| `FileIO` | 现在就尽量往 `BinaryBlob + handle + callback` 靠；可以先 C++ 导出，但不要把 STL 固化成接口契约 | 文件解析、写入、序列化天然适合 blob 化，后期收口成本低 |
| `UICommon` | 最适合先内部 C++、后 facade；但 `IDatabase` / `IBusinessDataRepository` 这类最好早点收口思路 | 业务接口最容易长期膨胀，建议尽早定义 ABI 形态 |
| `License` | 尽快做成稳定 C ABI | 通常跨语言、跨工具、跨版本更敏感，不适合长期停留在 C++ 导出 |
| `CrashHandler` | 尽早 C ABI 化 | 崩溃处理是系统边界，不适合复杂 C++ ABI |
| `Network` | 如果只是内部模块，可先 C++ 导出；如果要给外部客户端 / 插件 / 服务使用，最终还是要 C ABI | 根据实际使用范围决定收口节奏 |
| `SanYiRender` | 尽量保持 C ABI | 本身就适合作为稳定 facade，是最适合长期对外的层 |
| `Nesting` | 如果近期只作为内部模块，先 C++ 导出可以；但如果未来要开放给脚本 / 外部插件，就提前规划 facade | 先快后稳的典型候选 |
| `Vision` | 如果近期只作为内部模块，先 C++ 导出可以；但如果未来要开放给脚本 / 外部插件，就提前规划 facade | OpenCV 相关类型复杂，建议尽早明确边界 |

### 2.5 前期 C++ 导出、后期 C ABI 收口的迁移流程

> 这是推荐的分阶段实施方式：先用 C++ 导出把功能跑通，再按边界收口到 C ABI facade。

#### 阶段 A：先做 C++ 导出，但按“未来可收口”设计

1. **内部实现层允许使用 STL 和复杂 C++ 类型。**
2. **对外导出层尽量不要直接返回 `std::vector` / `std::string`。**
3. **如果暂时必须用 C++ 导出，优先使用 `const char*`、POD、`BinaryBlob`、回调、句柄。**
4. **不要把实现类、容器类型、异常语义当作长期 ABI 承诺。**
5. **头文件里尽量把“可替换成 C ABI”的边界先画好。**

#### 阶段 B：功能先完成，接口先稳定

1. **先把业务逻辑、算法、流程跑通。**
2. **C++ 导出层只承担过渡调用，不让下游直接依赖实现细节。**
3. **对外接口尽量收敛成少量统一入口。**
4. **测试先覆盖功能正确性，再覆盖 ABI 形态。**

#### 阶段 C：逐步收口成 C ABI facade

1. **把 C++ 导出层外包一层薄 facade。**
2. **将字符串、集合、二进制、复杂对象逐步替换为 `const char*`、`BinaryBlob`、POD、回调。**
3. **把外部调用方切到 facade 层。**
4. **将原 C++ 导出标记为内部或 deprecated。**
5. **最后再收紧 public 头文件，清理不必要的 C++ 暴露。**

#### 阶段 D：完成收口

1. **对外长期承诺的接口只保留 C ABI facade。**
2. **内部 C++ 接口只服务工程内联动，不再作为公开契约。**
3. **文档、测试、构建脚本同步更新。**

#### 迁移优先级建议

| 优先级 | 模块 | 迁移建议 |
|---|---|---|
| P0 | `License`、`CrashHandler`、`SanYiRender` | 优先收口，直接以稳定 facade 为目标 |
| P1 | `FileIO`、`UICommon` | 先 C++ 导出过渡，再按阶段收口 |
| P2 | `Network` | 按实际外部使用范围决定，内部可先 C++ 导出 |
| P3 | `Nesting`、`Vision` | 近期可先 C++ 导出，但应预留 facade 入口 |

### 3. `EngineCommon` 改造路线

> 说明：`ISceneManager`、`IEntity`、`IUndoRedoCommand`、`SceneRenderContract`、`SyEntity` 等 P0 纯虚接口已收口完成。当前 P2 关注点不再是纯虚接口本身，而是 `SceneManager` / `SceneManager3D` 仍保留的“统一接口 + 具体类型 API”双层面。下一步应优先厘清哪些具体类型方法属于内部实现面，哪些需要继续外提为稳定统一接口。

#### 2.1 目标

- 清理所有纯虚接口中的 `std::string` / `std::vector`
- 保留内部实现自由度，但不让 STL 进入跨 DLL ABI
- 将“返回集合 / 返回名称 / 设置名称 / 控制点读写”统一为 C ABI 友好形式

#### 2.2 推荐改法

| 场景 | 当前写法示例 | 推荐写法 |
|---|---|---|
| 枚举实体 ID | `virtual std::vector<EntityId> getAllEntityIds() const = 0;` | `virtual void forEachEntityId(bool(*visitor)(EntityId, void*), void* ctx) const = 0;` |
| 已选实体 ID | `virtual std::vector<EntityId> selectedEntityIds() const = 0;` | `virtual void forEachSelectedEntityId(bool(*visitor)(EntityId, void*), void* ctx) const = 0;` |
| 对象名称读取 | `virtual const std::string& name() const = 0;` | `virtual size_t name(char* buffer, size_t bufferSize) const = 0;` 或 `virtual const char* name() const = 0;` |
| 对象名称设置 | `virtual void setName(const std::string& name) = 0;` | `virtual void setName(const char* name) = 0;` |
| 命令描述 | `virtual std::string description() const = 0;` | `virtual size_t description(char* buffer, size_t bufferSize) const = 0;` |
| 控制点读取 | `virtual std::vector<Ut::Vec2d> getControlPoints() const = 0;` | `virtual void forEachControlPoint(void(*visitor)(const Ut::Vec2d*, void*), void* ctx) const = 0;` |
| 控制点设置 | `virtual void setControlPoints(const std::vector<Ut::Vec2d>&) = 0;` | `virtual void setControlPoints(const Ut::Vec2d* points, size_t count) = 0;` |

#### 2.3 执行步骤

1. 先清点所有 public / virtual 签名，列出 STL 命中点。
2. 新增必要的 visitor typedef、buffer 约定和 POD 参数类型。
3. 修改头文件接口签名，先不动内部实现逻辑。
4. 逐个修改所有派生类实现，保证编译通过。
5. 为名称读写、实体枚举、控制点读写补回归测试。
6. 检查 public 头文件，确认不再出现 STL 直接暴露。

#### 2.4 实施顺序建议

- 优先修复 `ISceneManager`、`IEntity`、`IUndoRedoCommand`、`SceneRenderContract`
- 再处理 `SyEntity` 的对外方法
- 最后统一收口所有派生实现和测试

### 3. `FileIO` 改造路线

#### 3.1 目标

- 将 FileIO 从“C++ 公共接口 + STL”收口为“内部实现 + C ABI facade”
- 序列化、加解密、文件读写统一使用 `BinaryBlob` / `char*` / 句柄
- 避免 `std::vector<uint8_t>`、`std::string` 直接穿越 DLL 边界

#### 3.2 推荐模块分层

| 层级 | 作用 | 允许使用 STL |
|---|---|---|
| 内部实现层 | parser/writer/serializer/crypto 的具体实现 | 允许 |
| 对外 facade 层 | 供其他 DLL / 外部调用 | 不允许 |
| 适配器层 | C ABI ↔ C++ 实现转换 | 内部可使用 |

#### 3.3 接口收口建议

| 组件 | 当前风险 | 建议改法 |
|---|---|---|
| `IFileParser` | `supportedExtensions()` / `formatName()` / `parse(const std::string&)` 含 STL | 改为回调枚举扩展名，字符串参数改 `const char*`，返回字符串改 buffer |
| `IFileWriter` | `formatName()` / `defaultExtension()` / `write(const std::string&)` 含 STL | 改为 buffer 返回和 `const char*` 入参 |
| `SyCryptoProvider` | `algorithmName()` / `encrypt(const std::vector<uint8_t>&)` / `decrypt(...)` | 改为 `BinaryBlob` 输入输出 + buffer 返回算法名 |
| `FileIOManager` | public 方法携带 `std::vector<std::string>` / `std::string` / `std::function` | 拆出 facade，public 头文件只保留句柄和 C ABI |
| `SySerializer` | 序列化/反序列化天然容易暴露 STL | 改为 `BinaryBlob` 输入输出，必要时增加句柄式对象流 |

#### 3.4 推荐基础类型

建议新增共享基础头，供各 facade 复用：

- `SanyiResult.h`
- `BinaryBlob.h`
- `CallbackTypes.h`
- `CharBuffer.h`

其中 `BinaryBlob` 可采用：

```cpp
struct BinaryBlob {
    const uint8_t* data;
    size_t size;
};
```

#### 3.5 执行步骤

1. 先冻结现有 FileIO public 头文件清单。
2. 先补 C ABI 基础类型，再改导出层。
3. 以 adapter 方式保留旧 C++ 实现。
4. 逐步把旧 STL 接口标记为内部或 deprecated。
5. 为文件解析、写入、加解密、序列化补回归测试。

### 4. `UICommon` 改造路线

#### 4.1 目标

- 将 `UICommon` 中的公共业务接口从 STL 公共暴露改为 C ABI / POD / 回调
- 把数据库、业务仓库、场景文档、场景构建、工具路径桥接分层处理
- 避免 `std::map<std::string, std::string>`、`std::vector<std::string>` 出现在稳定边界

#### 4.2 重点接口处理

| 组件 | 当前风险 | 建议改法 |
|---|---|---|
| `IDatabase` | 整个接口大量使用 `std::string` / `std::map` / `std::vector` | 优先重构为 C ABI facade，使用句柄、错误码、回调和 blob |
| `IBusinessDataRepository` | 同上，且典型为业务数据访问边界 | 优先重构为 C ABI facade，查询结果使用回调或结果句柄 |
| `SceneDocumentBase` | `allEntityIds()` 返回 `std::vector<std::string>` | 改为 `forEachEntityId(...)` 或 `entityIdAt(index, buffer, size)` |
| `SceneBuilderBase` | `defaultRootName()` 返回 `std::string` | 改为 `const char*` 或 buffer 返回 |
| `LaserToolpathBridge` | 工具路径批量参数容易直接使用 `std::vector` | 改为 POD point + 回调 / `BinaryBlob` |

#### 4.3 推荐接口形态

##### 方案 A：数据库 / 仓库采用 C ABI facade

```cpp
UICOMMON_API SanyiResult Database_Open(const char* connStr, DatabaseHandle* outHandle);
UICOMMON_API SanyiResult Database_Close(DatabaseHandle handle);
UICOMMON_API SanyiResult Repository_QueryByKey(
    RepositoryHandle repo,
    const char* key,
    bool(*visitor)(const BusinessRecord* record, void* ctx),
    void* ctx);
```

##### 方案 B：场景文档枚举改为回调

```cpp
virtual void forEachEntityId(bool(*visitor)(const char* entityId, void* ctx), void* ctx) const = 0;
```

##### 方案 C：默认名称采用 buffer 返回

```cpp
virtual size_t defaultRootName(char* buffer, size_t bufferSize) const = 0;
```

#### 4.4 执行步骤

1. 先判定 `UICommon` 中哪些接口必须对外，哪些只供内部使用。
2. 优先拆掉 `IDatabase`、`IBusinessDataRepository`。
3. 再收口 `SceneDocumentBase`、`SceneBuilderBase` 等轻量访问接口。
4. 把工具路径相关接口统一到 POD / Blob / callback。
5. 为数据库查询、场景枚举、工具路径输出补回归测试。

### 5. 接口级改造清单

> 本节把上面的路线图进一步拆成“逐个接口怎么改”，便于直接进入任务分解和代码修改。

#### 5.1 `EngineCommon` 接口清单

| 接口 | 当前问题 | 目标签名 / 目标形态 | 备注 |
|---|---|---|---|
| `ISceneManager::getAllEntityIds()` | 返回 `std::vector<EntityId>` | `void forEachEntityId(bool(*visitor)(EntityId, void*), void* ctx) const` | 以回调枚举代替集合返回 |
| `ISceneManager::selectedEntityIds()` | 返回 `std::vector<EntityId>` | `void forEachSelectedEntityId(bool(*visitor)(EntityId, void*), void* ctx) const` | 保留调用方自收集能力 |
| `IEntity::name()` | 返回 `const std::string&` | `size_t name(char* buffer, size_t bufferSize) const` 或 `const char* name() const` | 若内部名称会动态变化，优先 buffer 版 |
| `IEntity::setName()` | 接收 `const std::string&` | `void setName(const char* name)` | 避免 STL 入参穿越边界 |
| `IUndoRedoCommand::description()` | 返回 `std::string` | `size_t description(char* buffer, size_t bufferSize) const` | 适合撤销/重做菜单显示 |
| `SceneRenderContract::emitPolyline()` | 接收 `std::vector<Ut::Vec2d>` | `void emitPolyline(const Ut::Vec2d* points, size_t count, ...)` | 必要时再拆 begin/end 或批次参数 |
| `SceneRenderContract::emitText()` | 接收 `const std::string& text` | `void emitText(const char* text, ...)` | 文本类参数统一 `const char*` |
| `SceneRenderContract::emitTextEx()` | 接收 `const std::string& text` | `void emitTextEx(const char* text, ...)` | 同上 |
| `SceneRenderContract::emitTriangleSoup()` | 接收多个 `std::vector<Ut::Vec3f>` | `void emitTriangleSoup(const Ut::Vec3f* vertices, size_t vertexCount, const Ut::Vec3f* normals, size_t normalCount, ...)` | 复杂批数据一律 POD 化 |
| `SceneRenderContract::selectedEntityIds()` | 返回 `std::vector<EntityId>` | `void forEachSelectedEntityId(...)` | 与场景管理器保持一致 |
| `SceneRenderContract::entityName()` | 返回 `std::string` | `size_t entityName(uint64_t, char* buffer, size_t bufferSize) const` | 统一 buffer 输出 |
| `SyEntity::getControlPoints()` | 返回 `std::vector<Ut::Vec2d>` | `void forEachControlPoint(...) const` | 只暴露枚举，不暴露容器 |
| `SyEntity::setControlPoints()` | 接收 `const std::vector<Ut::Vec2d>&` | `void setControlPoints(const Ut::Vec2d* points, size_t count)` | 输入参数改为 POD 数组 |

#### 5.2 `FileIO` 接口清单

| 接口 | 当前问题 | 目标签名 / 目标形态 | 备注 |
|---|---|---|---|
| `IFileParser::supportedExtensions()` | 返回 `std::vector<std::string>` | `void forEachSupportedExtension(bool(*visitor)(const char*, void*), void* ctx) const` | 扩展名枚举改回调 |
| `IFileParser::formatName()` | 返回 `std::string` | `size_t formatName(char* buffer, size_t bufferSize) const` | 便于 ABI 稳定 |
| `IFileParser::parse()` | 接收 `const std::string& filePath` | `ParseResult parse(const char* filePath, ...)` | 文件路径统一 `const char*` |
| `IFileWriter::formatName()` | 返回 `std::string` | `size_t formatName(char* buffer, size_t bufferSize) const` | 与 parser 对齐 |
| `IFileWriter::defaultExtension()` | 返回 `std::string` | `size_t defaultExtension(char* buffer, size_t bufferSize) const` | 统一 buffer 输出 |
| `IFileWriter::write()` | 接收 `const std::string& filePath` | `bool write(const char* filePath, ...)` | 入参不再传 STL |
| `SyCryptoProvider::algorithmName()` | 返回 `std::string` | `size_t algorithmName(char* buffer, size_t bufferSize) const` | 算法名为只读元数据 |
| `SyCryptoProvider::encrypt()` | 接收 `const std::vector<uint8_t>&` | `CryptoResult encrypt(BinaryBlob input, BinaryBlobOut output)` | 以 blob 作为输入输出 |
| `SyCryptoProvider::decrypt()` | 接收 `const std::vector<uint8_t>&` | `CryptoResult decrypt(BinaryBlob input, BinaryBlobOut output)` | 同上 |
| `FileIOManager` 公共方法 | `std::string` / `std::vector<std::string>` / `std::function` 混用 | 对外仅保留句柄 + C API | 管理器本体转内部实现 |
| `SySerializer::serialize()` | 容易直接收 STL 容器 | `SanyiResult serialize(..., BinaryBlobOut out)` | 结果写入输出 blob |
| `SySerializer::deserialize()` | 容易直接收 STL 容器 | `SanyiResult deserialize(BinaryBlob in, ...)` | 输入 blob 化 |

#### 5.3 `UICommon` 接口清单

| 接口 | 当前问题 | 目标签名 / 目标形态 | 备注 |
|---|---|---|---|
| `IDatabase` 所有方法 | 大量使用 `std::string` / `std::map<std::string, std::string>` / `std::vector` | 重构为 C ABI facade | 建议不保留 STL 公开方法 |
| `IBusinessDataRepository` 所有方法 | 大量使用 `std::string` / 容器 | 重构为 C ABI facade | 查询结果用回调或句柄表示 |
| `SceneDocumentBase::allEntityIds()` | 返回 `std::vector<std::string>` | `void forEachEntityId(bool(*visitor)(const char*, void*), void* ctx) const` | 字符串 ID 枚举化 |
| `SceneBuilderBase::defaultRootName()` | 返回 `std::string` | `size_t defaultRootName(char* buffer, size_t bufferSize) const` | 与其他字符串接口一致 |
| `LaserToolpathBridge::executeLaserToolpath()` | 容易直接传 `std::vector` | `executeLaserToolpath(BinaryBlob toolpath, ...)` 或 `(..., const ToolpathPoint* points, size_t count, ...)` | 取决于工具路径格式 |

#### 5.4 接口级执行规则

1. **凡是返回集合的函数，优先改回调枚举，不直接返回 `std::vector`。**
2. **凡是返回字符串的函数，优先改 `char* buffer + size_t bufferSize`。**
3. **凡是接收文件路径、名称、键名、算法名等文本参数的函数，统一改 `const char*`。**
4. **凡是接收批量数值/点集/二进制数据的函数，统一改 `POD 数组 + count` 或 `BinaryBlob`。**
5. **若必须保留旧接口，必须把它降级为内部接口，不再作为 public ABI。**

### 6. 逐文件任务拆分表

> 本节把“接口级改造清单”进一步落成文件级任务，便于分配给开发者逐个修改。

#### 6.1 `EngineCommon` 文件级任务

| 文件 | 任务 | 优先级 | 说明 |
|---|---|---|---|
| `ISceneManager.h` | 替换 `getAllEntityIds()` / `selectedEntityIds()` 的 STL 返回值 | P0 | 改成回调枚举 |
| `IEntity.h` | 替换 `name()` / `setName()` 的 STL 字符串签名 | P0 | 改成 `const char*` / buffer |
| `IUndoRedoCommand.h` | 替换 `description()` 返回 `std::string` | P0 | 改成 buffer 输出 |
| `SceneRenderContract.h` | 替换 `emitPolyline()`、`emitText()`、`emitTextEx()`、`emitTriangleSoup()`、`entityName()` | P0 | 该文件是高风险 ABI 边界 |
| `SyEntity.h` | 替换 `getControlPoints()` / `setControlPoints()` | P0 | 改成回调 / 数组 + count |
| 对应 `.cpp` 实现 | 同步修改所有派生类实现 | P0 | 保证接口改完即可编译 |
| 相关测试 | 增加名称 / 枚举 / 控制点回归测试 | P1 | 覆盖新接口行为 |

#### 6.2 `FileIO` 文件级任务

| 文件 | 任务 | 优先级 | 说明 |
|---|---|---|---|
| `IFileParser.h` | 替换扩展名列表、格式名、解析路径参数 | P0 | 改成回调 + `const char*` |
| `IFileWriter.h` | 替换格式名、默认扩展名、写入路径参数 | P0 | 改成 buffer + `const char*` |
| `SyCryptoProvider.h` | 替换算法名、加解密数据签名 | P0 | 改成 `BinaryBlob` / `BinaryBlobOut` |
| `FileIOManager.h` | 收口 public API，避免 `std::vector<std::string>` / `std::function` | P0 | 管理器转内部实现或句柄 facade |
| `SySerializer.h` | 将 serialize / deserialize 改为 blob 化 | P0 | 避免 STL 穿越边界 |
| `FileIOExport.h` / `FileIOAPI.h` | 统一 C ABI facade 风格 | P0 | 检查导出宏、错误码、版本查询 |
| `FileIO` 对应实现文件 | 新增 C facade 适配层 | P0 | 外部调用只走 facade |
| 测试目录 | 增加解析 / 写入 / 加解密 / 序列化回归测试 | P1 | 验证新旧接口一致性 |

#### 6.3 `UICommon` 文件级任务

| 文件 | 任务 | 优先级 | 说明 |
|---|---|---|---|
| `IDatabase.h` | 彻底重构为 C ABI facade 或内部接口 | P0 | 不建议继续保留 STL 公开方法 |
| `IBusinessDataRepository.h` | 彻底重构为 C ABI facade 或内部接口 | P0 | 与数据库边界同步处理 |
| `SceneDocumentBase.h` | 替换 `allEntityIds()` | P0 | 改成回调枚举或索引访问 |
| `SceneBuilderBase.h` | 替换 `defaultRootName()` | P0 | 改成 buffer 输出 |
| `LaserToolpathBridge.h` | 收口工具路径参数 | P0 | 改成 POD 点集 / Blob |
| `UICommonAPI.h` | 统一导出宏与 C ABI 命名规则 | P0 | 作为整个模块的对外入口约束 |
| 相关实现文件 | 补 C ABI 适配器或内部转换层 | P0 | 先保留旧逻辑，再逐步收口 |
| 测试目录 | 增加数据库 / 场景枚举 / 工具路径回归测试 | P1 | 验证业务流不中断 |

#### 6.4 文件级执行顺序建议

1. **先改头文件签名，不先重写实现。**
2. **先改高风险纯虚接口，再改普通 public 类。**
3. **先改 public ABI，再改内部适配器。**
4. **最后统一修复测试和调用点。**

### 7. 建议执行顺序

1. **EngineCommon**：先清纯虚接口，风险最大，边界最明确。
2. **FileIO**：建立统一 facade 和 blob 基础类型，最容易形成规范。
3. **UICommon**：最后做业务层接口收口，改动最大但结构最清晰。

---

---

## 三十七、当前仍存在的风险项

> 来源：合并自 `DLL说明.md`（2026-08-11），仅保留未完全关闭的项。

| # | 风险项 | 状态 | 说明 |
|---|--------|------|------|
| 1 | `Hardware` / `Network` 以 Qt 类型（`QString`/`QStringList`/`QJsonObject`）作为跨 DLL 接口参数 | ⚠️ 待架构决策 | 依赖 Qt 二进制兼容；已标注"仅限内部同编译器/同构建体系"，见「ADR」 |
| 2 | `Vision` 虚继承 + OpenCV/STL 跨 DLL（`ShapeDetector` 返回 `std::vector<std::shared_ptr<Shape>>` 等） | ⚠️ 沙箱标注 | 已标注为沙箱内部模块，不承诺跨 DLL ABI；若对外发布需加 C 包装层 |
| 3 | `Main/Src/RenderCore/` 头文件混在 `Src/` | ❌ 未修复 | 建议抽到 `Main/Include/UiRenderCore/`，见「三十、Include 目录组织规范」 |
| 4 | 每个导出函数参数校验与异常屏蔽 | ⚠️ 部分完成 | CrashHandler/License 已完整；其余 C facade 建议逐步补齐 `try/catch(...)` + 前置校验 |
| 5 | 结构体带 `static_assert` 大小校验 | ❌ 未做 | 关键导出结构体建议增加 |
| 6 | 依赖检查纳入发布流程（`dumpbin /dependents` / `ldd`） | ❌ 未做 | 建议纳入 CI |
| 7 | `Render/` 旧 submodule 目录已废弃 | ⚠️ 已在 SanYiPaths.cmake 注释移除 | 渲染由 `Renderx/SanYiRender` 统一承担；旧 submodule 目录为空，不再构建 |
| 8 | 统一入口迁移后遗留：各模块 CMakeLists 仍保留 `LIB_SOURCES` 局部计数变量 | ✅ 已清理 | 迁移至 `sanyi_add_shared_library()` 时一并精简，见「十九、2」 |
| 9 | `Main/Src/UI/Workbench/WorkbenchMenuManager.cpp` 引用的 `IUiCommandDispatcher` 未定义（用户工作区进行中的重构） | ⚠️ 用户侧未完成 | 与 DLL 迁移无关，SanYiCAD.exe 编译受其阻塞 |
| 10 | MSVC `/m` 并行构建偶发 `error C2065` 误报（关联错位到无关行） | ⚠️ 已知 | Renderx `overlay_queue.cpp(515,59)` 曾误报 `rangeKind`，串行重编通过；遇此类错误先单线程复验 |

**已关闭项**（无需再处理）：

| 项目 | 状态 |
|------|------|
| FileIO `std::function` 回调跨 DLL（`ImportCallback`/`ExportCallback`） | ✅ 已收口（C1：C 函数指针 + `void* ctx`） |
| FileIO `IFileParser`/`IFileWriter` 返回 `std::vector<std::string>`/`std::string` | ✅ 已收口（C2/C4：回调/buffer/`const char*`） |
| `CrashHandler` 返回 `std::vector<std::string>` | ✅ 已修复（C API：`GetDumpFileCount`/`GetDumpFilePath`） |
| `CrashHandler` `std::string` 成员 ×5 | ✅ 已修复（固定缓冲区 + C API） |
| `CrashHandler` `std::function` 回调 | ✅ 已修复（C 函数指针 + `void* ctx`） |
| EngineCommon `SyEntity` 客户端 delete DLL 对象 | ✅ 已修复（`destroy()` + `SyEntityOwnPtr`） |
| GeoModelCore `shared_ptr`/`vector` 跨 DLL | ✅ 已收口（TopoShape PIMPL + 回调/buffer/`const char*`，见批次 E） |
| Vision `ShapeDetector` 返回 `shared_ptr` | ✅ 沙箱标注（不承诺跨 DLL ABI） |

---

*文档合并与重构时间：2026-08-11*
