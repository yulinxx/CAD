# Qt/C++ 项目测试体系搭建指南

> 本文档从 Qt UI 框架项目（Main）中提炼，目标是让**任何 CMake + Qt 的新项目**能在 30 分钟内搭起一套跑通的测试体系。
>
> 不需要读源码就能照抄。

---

## 一、整体架构：三层分工

```
┌──────────────────────────────────────────────────────────────────────┐
│  CTest（最外层，CMake 官方测试运行器）                               │
│                                                                      │
│  做什么：按清单 fork 进程、收集退出码、并行、标签筛选、超时、汇总报告 │
│                                                                      │
│  它不认识任何测试框架！它只跑命令。命令可以是：                       │
│    ├─ 编译出来的二进制（里面用 Qt Test / GTest / 任何框架写测试）    │
│    ├─ cmake -P xxx.cmake（静态分析脚本）                             │
│    └─ shell 脚本、Python 脚本……                                      │
└──────────────────────────────────────────────────────────────────────┘
                          │ 跑
                          ▼
┌──────────────────────────────────────────────────────────────────────┐
│  测试二进制（中间层，用单元测试框架写）                              │
│                                                                      │
│  每个二进制 = 一个独立的 main()（框架自动生成）                      │
│  退出码 0 → CTest 记 Passed                                         │
│  退出码非 0 → CTest 记 ***Failed                                    │
│                                                                      │
│  框架选择：                                                          │
│    Qt 项目 → Qt Test（`QTEST_MAIN` / `QTEST_GUILESS_MAIN`）         │
│    非 Qt 项目或想要更丰富的报告 → Google Test（`gtest_discover_tests`）│
└──────────────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────────────────┐
│  测试源码（内层，具体断言）                                          │
│                                                                      │
│  Qt Test 风格：                                                      │
│    class MyTest : QObject { Q_OBJECT private slots: void testX(); }; │
│    QTEST_GUILESS_MAIN(MyTest)                                        │
│                                                                      │
│  GTest 风格：                                                        │
│    TEST(MyTest, X) { EXPECT_EQ(...); }                               │
└──────────────────────────────────────────────────────────────────────┘
```

**关键理解**：CTest ≠ GTest/Qt Test。CTest 在上面跑，测试框架在下面被跑。两者是上下游关系，可以互换、可以混用。

---

## 二、搭建流程（3 步）

### 前置

- Qt 6.x（或 Qt 5.15+）
- CMake ≥ 3.20
- 能正常 configure + build 主工程

### 第 1 步：根 CMakeLists.txt —— 开一个开关

```cmake
# CMakeLists.txt —— 项目根目录

cmake_minimum_required(VERSION 3.20)
project(MyProject LANGUAGES CXX)

# ... 正常的 add_subdirectory(src) 等 ...

# 测试开关：不传就是 ON，方便本地和 CI；传 OFF 则跳过
option(MyProject_BUILD_TESTS "Build unit tests" ON)

if(MyProject_BUILD_TESTS)
    enable_testing()              # ← 关键一行！让 CMake 生成 CTest 清单
    add_subdirectory(tests)
endif()
```

### 第 2 步：tests/CMakeLists.txt —— 写封装函数 + 注册

**这一步把项目特定变量替换掉后，就是你可以在任何 Qt 项目里直接搬的通用版：**

```cmake
# tests/CMakeLists.txt

# ----------------------------------------------------------------
# 依赖：Qt Test（Qt 项目必带）
# ----------------------------------------------------------------
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Test)

# GUI 测试需要 Widgets 和 offscreen
# 注：Widgets 只在 msUiAddTest(... GUI) 时才链接，普通 Core 测试不浪费时间

# ----------------------------------------------------------------
# 统一注册函数：一行 = 一个测试
# ----------------------------------------------------------------
#
# 用法：
#   msAddTest(NAME Jsonc
#       SOURCES unit/Jsonc_test.cpp
#       LIBS MyCoreLib
#       LABELS unit)
#
#   msAddTest(NAME Dialog
#       SOURCES unit/Dialog_test.cpp
#       LIBS MyGuiLib
#       LABELS unit
#       GUI)    ← 需要 QApplication 时加，自动注入 offscreen
#
function(msAddTest)
    cmake_parse_arguments(ARG "GUI" "" "NAME;SOURCES;LIBS;LABELS" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_SOURCES)
        message(FATAL_ERROR "msAddTest: NAME 和 SOURCES 必须提供")
    endif()
    if(NOT ARG_LABELS)
        set(ARG_LABELS "unit")
    endif()

    # 目标名：tst_${NAME}（短名），避免和主工程 target 冲突
    set(_tst_target "tst_${ARG_NAME}")

    add_executable(${_tst_target} ${ARG_SOURCES})
    target_link_libraries(${_tst_target} PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        ${ARG_LIBS})

    add_test(NAME ${_tst_target} COMMAND ${_tst_target})
    set_tests_properties(${_tst_target} PROPERTIES LABELS "${ARG_LABELS}")

    # --- GUI 测试特殊处理 ---
    # offscreen 可避免真窗口，适合多数纯 Widgets 测试；依赖原生表面的路径
    # （如 AprilShell 完整 profile 的 MetalSurface/CenterView）在 offscreen 下失败，
    # 外壳冒烟需用最小 profile 排除该区域
    if(ARG_GUI)
        target_link_libraries(${_tst_target} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

        # Qt6：QtGui::PLUGIN_DIR 可以自动拿到 plugins 路径
        # Qt5：手动指定 ${Qt5_DIR}/../../../plugins
        set(_qt_plugin_dir "$<TARGET_PROPERTY:Qt${QT_VERSION_MAJOR}::Gui,PLUGIN_DIR>")
        set_tests_properties(${_tst_target} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_qt_plugin_dir}")
    endif()
endfunction()
```

**然后每个测试调一次 msAddTest：**

```cmake
# 例 1：纯 Core 测试，不需要 QApplication
msAddTest(NAME Jsonc
    SOURCES unit/Jsonc_test.cpp
    LIBS MyCoreLib)

# 例 2：GUI 测试，带 GUI 开关
msAddTest(NAME AboutDialog
    SOURCES unit/AboutDialog_test.cpp
    LIBS MyGuiLib
    GUI)

# 例 3：集成测试（标签不同）
msAddTest(NAME Integration
    SOURCES integration/tstIntegration.cpp
    LIBS MyCoreLib MyGuiLib
    LABELS integration
    GUI)
```

**需要额外 source 文件的情况**（内部头/源文件不导出）：

```cmake
# ProfileLocator 是 src/main/Launcher 下的内部类，不单独导出成库
# 直接把源文件编进测试二进制
msAddTest(NAME ProfileLocator
    SOURCES unit/Launcher_test.cpp
        "${PROJECT_SOURCE_DIR}/src/main/Launcher/ProfileLocator.cpp"
    LIBS Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(tst_ProfileLocator PRIVATE
    "${PROJECT_SOURCE_DIR}/src/main")   # ← 测试内 include "ProfileLocator.h"
```

**跨平台兼容的 Qt Quick 可选依赖**（Qt Quick 找不到就跳过，不让 CMake 整体挂）：

```cmake
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Qml Quick QuickWidgets)
if(TARGET Qt${QT_VERSION_MAJOR}::QuickWidgets)
    msAddTest(NAME QmlPanel
        SOURCES unit/QmlPanel_test.cpp
            "${PROJECT_SOURCE_DIR}/qml/PanelWidget.cpp"
            "${PROJECT_SOURCE_DIR}/qml/qml.qrc"
        LIBS MyCoreLib Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick
        GUI)
    # offscreen 无 GPU 时用软件渲染
    set_property(TEST tst_QmlPanel APPEND PROPERTY ENVIRONMENT
        "QT_QUICK_BACKEND=software")
else()
    message(STATUS "Qt Quick not found, skip QmlPanel test")
endif()
```

### 第 3 步：第一次配置 + 编译

```bash
# 1. 配置（一次就行，除非改了 CMakeLists.txt）
cmake -S . -B build -DMyProject_BUILD_TESTS=ON

# 2. 编译（改了代码都要跑）
cmake --build build -j4

# 3. 跑全部测试
cd build && ctest --output-on-failure
```

---

## 三、测试源码怎么写

### 目录约定

```
tests/
├── CMakeLists.txt              ← 只有一个，全部注册集中在此
├── unit/                       ← 单元测试（21 条那种）
│   ├── Model/                  ← 按模块分目录
│   │   ├── Jsonc_test.cpp
│   │   └── ProfileLoader_test.cpp
│   ├── i18n/
│   │   └── Translation_test.cpp
│   ├── Dialogs/
│   │   └── AboutDialog_test.cpp
│   └── ...
├── integration/                ← 集成交互测试（1 条那种）
│   └── tstIntegration.cpp
├── testHelpers/                ← 共用的假对象/桩（如果需要）
│   └── TestHelpers.cpp
└── resources/                  ← 测试专用 qss/qrc（如果需要）
    └── tstStyle.qrc
```

### Qt Test 代码模板

**纯 QtCore 测试**（不拉起 event loop，最快）：

```cpp
// tests/unit/Jsonc_test.cpp
#include <QtTest/QtTest>
#include "MyCore/Jsonc.h"

class JsoncTest : public QObject
{
    Q_OBJECT
private slots:
    void stripsLineComments()
    {
        const QString result = stripComments(QStringLiteral("x = 1 // trailing"));
        QCOMPARE(result, QStringLiteral("x = 1 "));
    }

    void keepsCommentsInsideStrings()
    {
        const QString result = stripComments(QStringLiteral("x = \"// not a comment\""));
        QCOMPARE(result, QStringLiteral("x = \"// not a comment\""));
    }
};

QTEST_GUILESS_MAIN(JsoncTest)
#include "Jsonc_test.moc"
```

**需要 QApplication 的 GUI 测试**：

```cpp
// tests/unit/AboutDialog_test.cpp
#include <QtTest/QtTest>
#include <QApplication>
#include "MyGui/AboutDialog.h"

class AboutDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void rendersProvidedFields()
    {
        AboutDialog dlg;
        dlg.setText(QStringLiteral("Hello"));
        QCOMPARE(dlg.label()->text(), QStringLiteral("Hello"));
    }
};

QTEST_MAIN(AboutDialogTest)     // ← 注意：这里是 QTEST_MAIN，不是 GUILESS
#include "AboutDialog_test.moc"
```

**数据驱动测试**（一组参数跑一个用例，`_data` 后缀触发 Qt Test 的 data 机制）：

```cpp
void MathTest::multiply_data()
{
    QTest::addColumn<int>("a");
    QTest::addColumn<int>("b");
    QTest::addColumn<int>("expected");

    QTest::newRow("zero")   << 0 << 5 << 0;
    QTest::newRow("one")    << 1 << 7 << 7;
    QTest::newRow("neg")    << -2 << 3 << -6;
    QTest::newRow("big")    << 100 << 100 << 10000;
}

void MathTest::multiply()
{
    QFETCH(int, a);
    QFETCH(int, b);
    QFETCH(int, expected);
    QCOMPARE(MathOps::multiply(a, b), expected);
}
```

### 静态分析脚本注册成 ctest

任何 `cmake -P` 脚本都可以注册成测试条目。退出码非 0 → CTest 记为 Failed：

```cmake
# tests/CMakeLists.txt 末尾
add_test(NAME CodeConventionCheck
    COMMAND ${CMAKE_COMMAND}
        -DMY_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P "${CMAKE_SOURCE_DIR}/cmake/CodeConventionCheck.cmake")
set_tests_properties(CodeConventionCheck PROPERTIES LABELS "unit")
```

脚本本体示例：

```cmake
# cmake/CodeConventionCheck.cmake
if(NOT MY_SOURCE_DIR) message(FATAL_ERROR "MY_SOURCE_DIR not set") endif()

set(_violations "")

# 扫描公开头，禁止引用 src/ 内部头
file(GLOB_RECURSE _pub_headers "${MY_SOURCE_DIR}/include/*.h")
foreach(_h ${_pub_headers})
    file(STRINGS "${_h}" _lines REGEX "#include.*src/")
    foreach(_line IN LISTS _lines)
        list(APPEND _violations "${_h}: ${_line}")
    endforeach()
endforeach()

list(LENGTH _violations _count)
if(_count GREATER 0)
    message(STATUS "Found ${_count} violation(s):")
    foreach(_v ${_violations})
        message(STATUS "  ${_v}")
    endforeach()
    message(FATAL_ERROR "Code convention check FAILED")   # ← 非 0 退出码
endif()

message(STATUS "Code convention check PASSED")
```

---

## 四、日常使用速查

```bash
# ============ 配置（一次，除非改了 CMakeLists.txt）============
cmake -S . -B build -DMyProject_BUILD_TESTS=ON

# ============ 编译（改了任何东西都要先跑）============
cmake --build build -j4

# ============ 跑测试 ============
cd build

ctest                              # 跑全部
ctest --output-on-failure          # 跑全部，失败时打印完整输出 ← 最常用
ctest -L unit                      # 只跑 unit 标签
ctest -L integration               # 只跑 integration 标签
ctest -R "tst_Jsonc"               # 只跑名字匹配的一条
ctest -R "Qml" -j4                 # 只跑 Qml 相关，4 进程并行
ctest --rerun-failed               # 只重跑上次失败的
ctest -V                           # verbose，每条都打印开始/结束
ctest -j4 --output-on-failure      # 4 进程并行 + 失败详情

# ============ 直接跑二进制（最详细输出）============
# ctest 有时会截断输出。直接跑 Qt Test 二进制能看到完整 PASS/FAIL 表格
./bin/tst_Jsonc                                  # 跑全部用例
./bin/tst_Jsonc -v                               # verbose，每个用例打 PASS/FAIL
./bin/tst_Jsonc stripsLineComments               # ← 只跑一个用例
./bin/tst_Dialog -skinCoversSharedWidgets        # ← 失败的那个单独看

# ============ 清理 + 重来 ============
rm -rf build && cmake -S . -B build -DMyProject_BUILD_TESTS=ON && cmake --build build -j4
```

---

## 五、常见坑

| 症状 | 原因 | 解决 |
|---|---|---|
| `ctest` 秒退出、什么都没跑 | 根 CMakeLists.txt 里忘写 `enable_testing()`  | 加上，然后重新 configure |
| GUI 测试弹真窗口、阻塞 | 没加 `GUI` 参数，或 `QT_QPA_PLATFORM=offscreen` 没注入 | `msAddTest(... GUI)` 自动注入 |
| GUI 测试报 `Could not connect to display` | CI 无 X server | 确保 `QT_QPA_PLATFORM=offscreen` 生效 |
| Qt Quick 测试报 `software renderer not found` | 无 GPU 的 CI 上缺 `QT_QUICK_BACKEND=software` | 加 `set_property(TEST ... APPEND PROPERTY ENVIRONMENT "QT_QUICK_BACKEND=software")` |
| 改了 `.qml` / `.ts` / `.qss` 后 ctest 还报旧错误 | 只跑了 ctest 没重新编 | `cmake --build .` 先触发 AUTORCC / lrelease |
| 二进制直接跑能过、ctest 里却失败 | 环境变量不同 | `ctest --output-on-failure -V` 看 CTest 注入的 ENVIRONMENT |
| 测试里 `#include "SomeInternal.h"` 编译不过 | 内部头没在 include 路径 | 在 `msAddTest` 后单独 `target_include_directories(tst_xxx PRIVATE "...")` |
| 并行跑有时失败（flaky） | 多进程共享状态竞争 | `ctest -R 名字 -j1` 关掉并行，定位后再修 |

---

## 六、完整文件清单（搬模板时需要的）

| 文件 | 在哪 | 做什么 |
|---|---|---|
| 根 `CMakeLists.txt` | 项目根 | 加两行：`enable_testing()` + `add_subdirectory(tests)` + 开关 |
| `tests/CMakeLists.txt` | tests/ | 封装函数 `msAddTest` + 每条测试一行注册 |
| `tests/unit/*_test.cpp` | tests/unit/XXX/ | 每个测试二进制一个文件 |
| `cmake/CodeChecks.cmake`（可选） | cmake/ | 静态分析脚本，注册成 ctest 条目 |

不需要的东西：testHelpers（桩）、resources（测试专用资源）—— 需要再加。

---

## 七、验收 checklist

搭完以后按这个清单逐条验证：

- [ ] `cmake -S . -B build -DMyProject_BUILD_TESTS=ON` 不报错
- [ ] `cmake --build build -j4` 编译全部测试二进制
- [ ] `ctest` 至少跑通 1 条纯 Core 测试（如 Jsonc）
- [ ] 至少跑通 1 条 GUI 测试（不弹真窗口）
- [ ] `ctest -L unit` 和 `ctest -L integration` 标签筛选正常
- [ ] `ctest -R "tst_Xxx"` 只跑指定测试
- [ ] `cmake -DMyProject_BUILD_TESTS=OFF` 能正常 configure + build 不带测试的版本

全部 ✅ 后，这套体系就可以作为其他项目的模板了。
