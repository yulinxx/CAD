# 第三方许可证合规与 SBOM 指南

> 状态：**现状记录 + 待整改清单**（2026-10-21）
>
> 本文用于建立许可证与 SBOM 的基础认知、盘点当前工程的实际风险，并给出后续整改路线。
> **本文不是法律意见，也不能替代法务/合规评审。** 文中的“高风险/中风险”是工程视角的
> 风险提示，最终结论必须由公司法务或外部律师结合商业模式、分发方式、合同义务确认。
>
> 当前阶段先**只记录、不整改**：不改代码、不改打包、不替换第三方库。等法务结论明确后，
> 再按本文第 9 节的路线推进。

---

## 1. 为什么这件事和 CAD 工程有关

SanYi CAD 是一个**闭源商业软件**：

- 根目录 `LICENSE.md` 写明 `Proprietary License`，禁止未经授权的复制、修改、分发；
- 工程包含 LicenseManager、KeygenTool 等商业授权机制；
- 安装包通过 CPack 生成 NSIS / DMG / DEB / RPM / AppImage，对外分发二进制。

只要**对外分发**了包含第三方代码的二进制，就必须遵守这些第三方代码的许可证。
“代码是我们写的”不等于“整个安装包都是我们的”。第三方库的许可义务会跟随二进制一起分发。

最容易出问题的不是“用了开源库”，而是：

1. 不知道用了哪些库、什么版本、什么许可证；
2. 把 **GPL/AGPL** 代码直接编进闭源程序；
3. 安装包里没有版权声明、许可证全文、源码获取方式；
4. 修改了第三方代码却没有记录；
5. 以为“动态链接”“内部使用”“放在 GitHub 上”就自动没问题。

---

## 2. 先建立几个基本概念

### 2.1 版权与许可证

- **版权（Copyright）**：代码的作者默认拥有复制、修改、分发权。
- **许可证（License）**：作者授权你在特定条件下使用代码。没有许可证，默认你不能复制、修改、分发。
- **许可证不是“免费声明”**：它同时包含**权利**和**义务**。免费使用不等于没有义务。

### 2.2 三类常见许可证

| 类型 | 代表 | 核心特点 | 对闭源商业软件的影响 |
|------|------|----------|----------------------|
| **宽松型（Permissive）** | MIT、BSD、Apache-2.0、BSL-1.0、Zlib | 允许闭源、商用、修改、再分发，但要求保留版权/许可声明，部分要求 NOTICE 或标注修改 | 通常可用，但**必须做声明归档** |
| **弱 copyleft** | LGPL-2.1、LGPL-3.0、MPL-2.0 | 允许闭源程序动态链接，但对被链接库本身有源码/替换/声明义务 | 可用，但**有明确合规动作**，不能“动态链接就完事” |
| **强 copyleft** | GPL-2.0、GPL-3.0、AGPL-3.0 | 分发衍生作品时，要求以相同许可证提供完整对应源码 | 与闭源商业分发**天然冲突**，必须法务评审或替换/隔离/商业授权 |

### 2.3 “衍生作品”和“聚合”

这是 GPL 场景最关键、也最容易争议的概念：

- **衍生作品（Derivative Work）**：把 GPL 代码和你的代码结合成一个整体，通常认为整体要按 GPL 分发。
- **聚合（Mere Aggregation）**：两个独立程序放在同一张光盘/同一个安装包里，但彼此不是紧密耦合的整体，可能仍可各自保持原许可证。

边界取决于**耦合程度**：是否互相调用、是否共享数据结构、是否编译进同一个二进制、是否形成单一作品。
**这不是工程上能自行下结论的问题，必须让法务判断。**

本项目的 `libdxfrw` 不是独立进程，而是通过 `add_library`/源文件列表直接编进 `FileIO` 目标
（见 `FileIO/FileIO/CMakeLists.txt`），因此不能简单用“它只是一个库”来淡化风险。

### 2.4 静态链接、动态链接、进程隔离

| 使用方式 | 典型义务 | 工程含义 |
|----------|----------|----------|
| **静态链接** | 代码被复制进最终二进制，copyleft 义务最强 | 最容易触发“衍生作品”判断 |
| **动态链接** | 代码在运行时加载；LGPL 通常允许，但有替换/声明义务 | 不代表 GPL 一定安全，仍需看许可证和耦合方式 |
| **独立进程 + 进程间通信** | 更接近“聚合”，但仍取决于通信方式和整体设计 | 常被用作隔离 GPL 的工程手段，但需法务确认 |
| **SaaS/网络服务** | AGPL 可能因“网络交互”触发源码提供义务 | 桌面软件也要看是否对外提供网络服务 |

### 2.5 分发（Distribution）

大多数义务由“分发”触发：

- 把安装包发给客户、代理商、试用用户，通常都算分发；
- 只在公司内部使用、不对外提供，义务通常弱一些，但仍可能受合同、出口管制、商标等约束；
- 把二进制放到官网下载、网盘、客户现场部署，都应按“对外分发”准备合规材料。

**不要用“我们只给客户装，不公开下载”来判断不需要合规。** 客户也是接收方。

---

## 3. 常见许可证速查

> 下表是工程视角的简化说明，不构成法律意见。具体版本、例外条款、双许可选项必须以
> 组件自带的 `LICENSE` / `COPYING` / `NOTICE` 为准。

| SPDX | 许可证 | 闭源商用 | 主要义务 | 备注 |
|------|--------|----------|----------|------|
| MIT | MIT License | 可以 | 保留版权声明和许可声明 | 最简单、最常见 |
| BSD-2-Clause | BSD 2-Clause | 可以 | 保留版权声明和许可声明 | 与 MIT 接近 |
| BSD-3-Clause | BSD 3-Clause | 可以 | 保留声明；不得用作者名背书 | Google Breakpad、protobuf、GTest 常用 |
| Apache-2.0 | Apache License 2.0 | 可以 | 保留 LICENSE/NOTICE；标注修改；专利授权 | 与 GPLv2 **不兼容**，与 GPLv3 兼容 |
| BSL-1.0 | Boost Software License | 可以 | 保留许可声明 | 非常宽松，无需 NOTICE |
| Zlib | zlib License | 可以 | 保留声明；不得冒充作者 | NanoSVG 等 |
| Unlicense/CC0 | 公共领域声明 | 可以 | 通常无义务 | 仍建议保留来源记录 |
| LGPL-2.1 | GNU Lesser GPL 2.1 | 有条件可以 | 动态链接、允许替换库、提供库源码/修改、声明 | OpenCASCADE 使用带例外条款的 LGPL-2.1 |
| LGPL-3.0 | GNU Lesser GPL 3.0 | 有条件可以 | 同上，另有安装信息/反规避条款 | Qt 开源版常用 LGPL-3.0 |
| MPL-2.0 | Mozilla Public License 2.0 | 可以 | 修改过的 MPL 文件需公开源码 | 文件级 copyleft |
| GPL-2.0 | GNU GPL 2.0 | **高风险** | 分发衍生作品需按 GPL 提供完整源码 | libdxfrw 为 GPL-2.0-or-later |
| GPL-3.0 | GNU GPL 3.0 | **高风险** | 同上，另有专利/反规避条款 | 与 Apache-2.0 兼容 |
| AGPL-3.0 | GNU Affero GPL 3.0 | **高风险** | GPL 义务 + 网络交互也触发源码提供 | Ghostscript 当前许可 |
| 商业许可 | 各厂商 EULA | 按合同 | 按合同付费/限制 | Qt、Ghostscript 等可购买商业许可 |

### 3.1 GPL-2.0-or-later 是什么意思

`libdxfrw` 的源码头写明：

> either version 2 of the License, or (at your option) any later version

这意味着使用方可以选择按 **GPLv2** 或 **GPLv3**（或未来版本）来遵守。
“or later”不会让 GPL 变成宽松许可，它仍然要求 copyleft；它只是给了你选择更高版本条款的空间，
在某些许可证兼容性场景下有意义（例如 Apache-2.0 与 GPLv3 兼容，但与 GPLv2 不兼容）。

### 3.2 LGPL 不是“随便动态链接”

LGPL 的典型合规动作包括：

1. 在安装包/文档中声明使用了该 LGPL 库，并附许可证全文；
2. 让用户能够替换该库（动态链接通常可以，但不能阻止替换）；
3. 如果修改了 LGPL 库本身，需要提供修改后的源码；
4. LGPLv3 还涉及“安装信息”和反规避条款（例如用户产品中的锁定）；
5. 不能把 LGPL 代码静态链接进闭源程序后当作没发生。

Qt 开源版是 LGPLv3/GPLv3 双许可（部分模块不同），商业闭源使用通常选择：
**动态链接 + LGPLv3 合规**，或**购买 Qt 商业许可**。两条路都要做，不能只做一半。

### 3.3 AGPL 比 GPL 多什么

AGPL-3.0 在 GPL 基础上增加了“网络交互”条款：如果用户通过网络与修改后的程序交互，
也要提供对应源码。对桌面 CAD 来说，如果只是本地运行、不提供网络服务，AGPL 的主要义务
仍来自**分发**；但 Ghostscript 作为随包分发的二进制，分发义务本身就需要认真处理。

---

## 4. 当前工程的实际依赖盘点

> 这一节是“已知信息 + 待核对信息”的混合。带“待核对”的条目不能当作最终结论。
> 版本以实际构建产物、vcpkg 安装记录、`ThirdParty` 子模块提交为准。

### 4.1 直接编进项目二进制的组件

| 组件 | 位置/使用方式 | 许可证 | 分发形态 | 当前判断 |
|------|----------------|--------|----------|----------|
| **libdxfrw** | `ThirdParty/libdxfrw`，源码通过 `FileIO/FileIO/CMakeLists.txt` 的源文件列表直接编进 `FileIO` | **GPL-2.0-or-later**（`COPYING` 为 GPLv2 文本，源码头为 or-later） | 编进闭源 DLL | **高风险，需法务评审** |
| Qt 6 | 本地 Qt 安装，`windeployqt` 复制 DLL/插件 | **LGPL-3.0 / GPL-3.0 / 商业许可**（按实际授权） | 动态链接 + 随包 DLL | 中风险，需确认授权方式与 LGPL 合规 |
| OpenCASCADE | vcpkg，`GeoModelCore` 使用 | **LGPL-2.1 + 例外条款** | 动态链接 | 中风险，需声明/源码提供方式 |
| Clipper2 | vcpkg，Nesting/几何运算 | **BSL-1.0** | 动态/静态链接 | 低风险，保留声明 |
| SQLiteCpp | vcpkg，UICommon 等 | **MIT** | 链接 | 低风险，保留声明 |
| NanoSVG | vcpkg，FileIO | **Zlib** | 编进库 | 低风险，保留声明 |
| stb | vcpkg，图像/字体 | **MIT / Public Domain** | 编进库 | 低风险，保留声明 |
| OpenSSL | vcpkg，License/Network | **Apache-2.0** | 动态链接 | 低风险，保留 LICENSE/NOTICE |
| protobuf | vcpkg，持久化 | **BSD-3-Clause** | 动态/静态链接 | 低风险，保留声明 |
| spdlog | vcpkg，Log | **MIT** | 链接 | 低风险，保留声明 |
| Boost | vcpkg，几何/工具 | **BSL-1.0** | 头文件/链接 | 低风险，保留声明 |
| freetype | vcpkg，字体 | **FTL 或 GPLv2**（双许可） | 动态链接 | 需确认实际选择的许可 |
| WebP | vcpkg，FileIO | **BSD-3-Clause** | 链接 | 低风险 |
| libtiff | vcpkg，FileIO | **BSD 类** | 链接 | 低风险，需核对具体条款 |
| GTest / GMock | vcpkg，测试 | **BSD-3-Clause** | 仅测试，不进安装包 | 低风险 |
| unofficial-breakpad | vcpkg，CrashHandler | **BSD-3-Clause** | 动态链接 | 低风险，保留声明 |
| pybind11 | vcpkg，Python 绑定 | **BSD-3-Clause** | 动态/静态链接 | 低风险 |
| Python | vcpkg/系统，PythonHost | **PSF-2.0** | 运行时/嵌入 | 需确认分发方式 |

### 4.2 随安装包分发的独立工具

| 组件 | 位置/使用方式 | 许可证 | 当前判断 |
|------|----------------|--------|----------|
| **poppler** | `ThirdParty/poppler/<platform>/<arch>/`，`Main/CMakeLists.txt` 部署 bin/lib | **GPL-2.0 / GPL-3.0**（取决于版本与组件） | **高风险，需按实际二进制核对版本和许可** |
| **ghostscript** | `ThirdParty/ghostscript/<platform>/<arch>/`，`Main/CMakeLists.txt` 部署 bin/lib | **AGPL-3.0**（Artifex 当前许可） | **高风险，需按实际二进制核对版本和许可** |
| SVGnest | `ThirdParty/SVGnest`，是否进入安装包待确认 | **MIT** | 低风险，若分发需保留声明 |
| QrCodeGen | `ThirdParty/QrCodeGen`，是否进入安装包待确认 | 待核对（多语言实现可能不同） | 需确认是否使用及版本 |

### 4.3 当前仓库里“缺失的记录”

- `ThirdParty/README.md` 只覆盖了 5 个库（Clipper2、SQLiteCpp、libdxfrw、NanoSVG、stb），
  没有覆盖 Qt、OpenCASCADE、OpenCV、OpenSSL、protobuf、spdlog、Breakpad、poppler、Ghostscript 等；
- 没有统一的**版本清单**（vcpkg 没有 `vcpkg.json` manifest，依赖靠手工 `vcpkg install`）；
- 没有 `THIRD-PARTY-NOTICES` / `NOTICE` / `COPYING` 汇总；
- `CMake/Packaging.cmake` 只把项目自己的 `LICENSE.md` 作为安装包许可文件；
- `ThirdParty/README.md` 里“若使用 GPL 库（如 libdxfrw），确认项目分发合规性”仍是未完成事项；
- 没有 SBOM，也没有 CI 许可证门禁。

---

## 5. 风险分级

> 分级是工程视角，不是法律结论。最终以法务意见为准。

| 等级 | 场景 | 本项目对应 |
|------|------|------------|
| **高** | GPL/AGPL 代码被编进闭源二进制，或随闭源安装包分发且没有合规方案 | libdxfrw（编进 FileIO）；poppler / ghostscript（随包分发） |
| **中** | LGPL 动态链接但缺少声明、替换能力、源码提供；或商业许可与开源许可混用未确认 | Qt、OpenCASCADE |
| **低** | 宽松许可但缺少版权/许可声明、NOTICE | MIT/BSD/Apache/BSL/Zlib 依赖 |
| **未知** | 版本、许可证、是否分发未记录 | QrCodeGen、SVGnest、部分 vcpkg 传递依赖 |

### 5.1 为什么 libdxfrw 是最高优先级

- 它不是“独立进程调用”，而是源码直接参与 `FileIO` 编译；
- `FileIO` 是主程序依赖的核心模块，最终进入 `SanYiCAD` 安装包；
- GPL-2.0-or-later 要求分发衍生作品时提供完整对应源码；
- 项目是 Proprietary，和 GPL 的 copyleft 目标直接冲突；
- 除非能证明它只是“聚合”且不构成衍生作品，否则风险很高；这个判断必须由法务做。

### 5.2 为什么 poppler / Ghostscript 也不能忽略

- 它们以二进制形式随安装包分发，不是“用户自己安装的系统工具”；
- 仓库里没有记录具体版本，也没有附带它们的 LICENSE/COPYING；
- Ghostscript 当前是 AGPL-3.0，义务比 GPL 更强；
- 即使它们作为独立可执行文件被调用，也可能涉及“聚合”与“衍生”的边界判断；
- 如果未来改成静态链接或深度集成，风险会进一步上升。

### 5.3 Qt / OpenCASCADE 是“可管理”的中风险

LGPL 并不禁止闭源商用，但要求你**真的做到**：

- 动态链接，不阻止用户替换库；
- 在安装包/关于页面/文档中提供版权声明和许可证全文；
- 对修改过的库提供源码；
- 遵守 LGPLv3 的安装信息/反规避条款；
- 保留源码获取方式（或书面要约）。

如果采购了 Qt 商业许可，Qt 部分按商业合同执行；但 OpenCASCADE、libdxfrw、poppler、
Ghostscript 的义务不会因为买了 Qt 商业许可而消失。

---

## 6. SBOM 是什么

### 6.1 定义

**SBOM（Software Bill of Materials，软件物料清单）** 是软件产品的“配料表”：
列出产品里包含哪些组件、什么版本、来自哪里、什么许可证、以什么方式使用。

SBOM 不等于许可证合规，也不等于安全扫描。它解决的是“**我到底用了什么**”这个最基础的问题。
没有 SBOM，许可证合规、漏洞响应、供应链审计都只能靠猜。

### 6.2 常见标准

| 标准 | 特点 | 适用 |
|------|------|------|
| **SPDX** | ISO/IEC 5962 标准，许可证表达能力强 | 法务/合规、对外交付 |
| **CycloneDX** | 轻量、JSON/XML、安全场景友好 | 工程 CI、漏洞管理、客户安全审查 |
| **SWID** | 软件标识标签 | 资产管理系统 |

很多公司会同时生成 SPDX 和 CycloneDX；本项目建议先做 CycloneDX，后续按客户要求补 SPDX。

### 6.3 最小字段

一个能用的 SBOM 至少应包含：

- 组件名称、版本、供应商/来源 URL；
- 许可证（SPDX ID，未知要标 `NOASSERTION`）；
- 使用方式：运行时/构建时/测试时、静态/动态链接、是否修改；
- 唯一标识：PURL（如 `pkg:github/LibreCAD/libdxfrw@...`）或 CPE；
- 文件哈希（可选但推荐）；
- 组件之间的关系（谁依赖谁）；
- SBOM 生成时间、工具、目标产品版本。

### 6.4 生成方式（后续落地时可选）

| 工具/来源 | 用途 | 备注 |
|-----------|------|------|
| `vcpkg.json` + vcpkg 依赖信息 | 记录 vcpkg 依赖及版本 | 当前项目还没有 manifest，需先补 |
| `compile_commands.json` | 确认实际参与编译的源文件 | 需要在 CMake 中开启导出 |
| ScanCode Toolkit | 扫描源码中的许可证和版权 | 适合 `ThirdParty/` 这种 vendored 代码 |
| ORT (OSS Review Toolkit) | 完整合规流程：扫描、依赖解析、规则引擎 | 偏重，适合正式合规团队 |
| Syft | 从目录/镜像生成 SBOM | 可扫描二进制和文件系统 |
| cdxgen | 生成 CycloneDX SBOM | 支持多种语言/构建系统 |
| CycloneDX CLI | SBOM 合并、校验、格式转换 | 适合 CI |

**注意**：自动扫描会有误报/漏报。SBOM 必须和人工复核、法务审批结合，不能只看工具输出。

### 6.5 SBOM 示例（CycloneDX 简化）

```json
{
  "bomFormat": "CycloneDX",
  "specVersion": "1.5",
  "version": 1,
  "metadata": {
    "timestamp": "2026-10-21T00:00:00Z",
    "component": {
      "type": "application",
      "name": "SanYiCAD",
      "version": "1.0.0"
    }
  },
  "components": [
    {
      "type": "library",
      "name": "libdxfrw",
      "version": "2.0.0",
      "licenses": [{ "license": { "id": "GPL-2.0-or-later" } }],
      "purl": "pkg:github/LibreCAD/libdxfrw@2.0.0",
      "properties": [
        { "name": "sanyi:usage", "value": "compiled-into-FileIO" }
      ]
    },
    {
      "type": "library",
      "name": "Qt",
      "version": "6.11.1",
      "licenses": [{ "license": { "id": "LGPL-3.0-only" } }],
      "properties": [
        { "name": "sanyi:usage", "value": "dynamic-link" }
      ]
    }
  ]
}
```

上面只是格式示例，不是本项目的最终 SBOM；版本和许可证必须以实际构建为准。

---

## 7. 安装包里的 NOTICE 应该包含什么

一个可交付的 `THIRD-PARTY-NOTICES.txt` 至少应包含：

1. 产品名称、版本、生成日期；
2. 每个第三方组件的名称、版本、来源 URL、许可证 SPDX ID；
3. 每个许可证的**全文**（或随包附 `licenses/` 目录）；
4. 对 GPL/LGPL/AGPL 组件，说明源码获取方式（URL、书面要约、有效期）；
5. 对修改过的组件，说明修改内容和修改日期；
6. 如果使用了 Apache-2.0 组件，保留其 `NOTICE` 内容；
7. 联系方式（合规/法务邮箱）。

### 7.1 推荐目录结构

```text
安装目录/
├─ SanYiCAD.exe
├─ LICENSE.md                      # 项目自身许可
├─ THIRD-PARTY-NOTICES.txt         # 第三方组件清单 + 声明
└─ licenses/
   ├─ GPL-2.0.txt
   ├─ LGPL-3.0.txt
   ├─ AGPL-3.0.txt
   ├─ Apache-2.0.txt
   ├─ MIT.txt
   └─ ...
```

### 7.2 CPack 接入位置（后续做，不在本次范围）

当前 `CMake/Packaging.cmake` 只设置了：

```cmake
set(CPACK_RESOURCE_FILE_LICENSE "${SANYI_ROOT}/LICENSE.md")
```

后续整改时应在 `install()` 中增加第三方 NOTICE 和 `licenses/` 目录，并确保它们进入
NSIS/DMG/DEB 的安装内容。不要在本次改动中提前加入空的 NOTICE 文件，以免造成“已经合规”的错觉。

---

## 8. 许可证门禁应该怎么设计（后续做）

建议把门禁分成“**新增依赖**”和“**存量依赖**”两条线：

### 8.1 新增依赖

- 新增任何第三方库，必须在 PR 中填写：名称、版本、来源、许可证、使用方式、是否修改；
- 默认策略：
  - MIT/BSD/Apache-2.0/BSL-1.0/Zlib：允许，但必须登记 NOTICE；
  - LGPL：需要架构/法务确认，并给出动态链接/替换/源码提供方案；
  - GPL/AGPL：默认拒绝，除非有法务批准或明确的隔离/商业授权方案；
  - 未知许可证：拒绝；
- CI 检查：许可证字段缺失、SBOM 未更新、NOTICE 未更新时失败。

### 8.2 存量依赖

- 先建立完整清单，不要求一次清零；
- 用“棘轮”方式：新增违规为 0，存量逐项登记、逐项关闭；
- 每次 release 生成 SBOM，和安装包、符号文件一起归档；
- 每季度复核一次许可证/版本变化。

### 8.3 推荐的 CI 检查顺序

```text
1. 解析依赖清单 / vcpkg manifest
2. 生成 SBOM（CycloneDX）
3. 许可证策略校验（allow/deny/conditional）
4. NOTICE 完整性校验
5. 未知许可证/未知版本告警
6. 产物归档：SBOM + NOTICE + 安装包 + 符号文件
```

门禁不要只做“关键词匹配”。例如 `GPL-2.0-or-later`、`LGPL-2.1+`、`Apache-2.0 WITH LLVM-exception`
这类 SPDX 表达式必须按表达式解析，不能简单 `grep GPL`。

---

## 9. 建议的整改路线（本次不执行）

> 这一节是后续计划，不是当前改动。当前只建立文档和认知。

### 阶段 0：信息补齐（1 周内可做）

- [ ] 建立完整依赖清单：名称、版本、来源、许可证、使用方式、是否分发；
- [ ] 确认 `ThirdParty/poppler`、`ThirdParty/ghostscript` 的实际版本和许可证；
- [ ] 确认 `libdxfrw` 是否被修改过（对比上游 commit / `LIBRECAD_SYNC.md`）；
- [ ] 确认 QrCodeGen、SVGnest 是否进入最终安装包；
- [ ] 把所有依赖的 LICENSE/COPYING 原文归档到仓库或构建产物。

### 阶段 1：法务评审（必须）

- [ ] 评审 libdxfrw 编进 `FileIO` 的 GPL 风险；
- [ ] 评审 poppler / Ghostscript 随包分发的 GPL/AGPL 风险；
- [ ] 评审 Qt 的授权方式（LGPL 合规 vs 商业许可）；
- [ ] 评审 OpenCASCADE LGPL 例外条款的适用方式；
- [ ] 形成书面结论：哪些可以继续用、哪些必须替换/隔离/购买商业许可。

### 阶段 2：技术方案（法务结论后）

- [ ] libdxfrw：替换为宽松许可的 DXF 库 / 独立进程隔离 / 购买商业许可 / 其他方案；
- [ ] poppler / Ghostscript：购买商业许可 / 替换 / 隔离 / 合规分发；
- [ ] Qt：确定 LGPL 合规方案或商业许可；
- [ ] 所有 LGPL 组件：确认动态链接、替换能力、源码提供方式。

### 阶段 3：交付物

- [ ] 增加 `THIRD-PARTY-NOTICES.txt` 和 `licenses/`；
- [ ] 安装包附 NOTICE；
- [ ] 每次 release 生成 SBOM 并归档；
- [ ] 关于页面/帮助菜单提供“开源许可”入口；
- [ ] 客户安全审查时能直接提供 SBOM。

### 阶段 4：持续门禁

- [ ] 新增依赖审批流程；
- [ ] CI 许可证策略检查；
- [ ] 季度复核；
- [ ] 依赖升级时同步更新 SBOM/NOTICE。

---

## 10. 常见误区

### “开源就是免费，随便用”

错。开源许可证是版权许可，不是放弃版权。宽松许可也有声明义务，copyleft 有源码义务。

### “我们动态链接 GPL，所以没问题”

错。GPL 是否构成衍生作品，取决于耦合方式，不是只看链接方式。动态链接也可能被认定为衍生作品。
LGPL 才是为“闭源程序动态链接库”设计的弱 copyleft 许可，但 LGPL 也有自己的义务。

### “我们只在公司内部用，不分发”

内部使用通常不触发 GPL 的“分发”义务，但仍可能受合同、商业秘密、出口管制、商标等约束；
一旦把软件交给客户、代理商、试用用户，就应按分发准备。不要用“内部使用”覆盖客户交付。

### “我们在 GitHub 上放了源码链接，就够了”

GPL 要求提供“对应源码”，不是只给一个仓库首页链接。源码版本必须与二进制对应，
并且要能让接收方实际获取。AGPL 还涉及网络交互场景。

### “买了 Qt 商业许可，所有第三方都解决了”

不。Qt 商业许可只解决 Qt 本身；libdxfrw、poppler、Ghostscript、OpenCASCADE 等仍需单独处理。

### “SBOM 出来了，就合规了”

不。SBOM 只是清单。许可证判断、NOTICE、源码提供、商业授权、法务审批仍然要做。

### “许可证扫描工具说没问题，就没问题”

不。工具会误报/漏报，尤其是 vendored 代码、修改过的代码、双许可、带例外条款的许可证。
工具输出必须人工复核。

### “改一点第三方代码没关系”

修改 GPL/LGPL 代码后，义务可能更强：需要标注修改、提供修改后的源码。必须记录 diff 和日期。

---

## 11. 术语表

| 术语 | 含义 |
|------|------|
| **Copyright** | 版权，作者对作品复制/修改/分发的专有权 |
| **License** | 许可证，作者授予的使用条件 |
| **Permissive** | 宽松许可，允许闭源再分发，通常只要求保留声明 |
| **Copyleft** | 著佐权，要求衍生作品以相同许可证开放 |
| **Derivative Work** | 衍生作品，对原作品的修改/结合形成的作品 |
| **Mere Aggregation** | 聚合，多个独立程序放在一起但未形成单一作品 |
| **Static Linking** | 静态链接，库代码被复制进最终二进制 |
| **Dynamic Linking** | 动态链接，运行时加载共享库 |
| **Distribution** | 分发，把软件副本提供给第三方 |
| **Source Offer** | 源码要约，书面承诺提供对应源码 |
| **NOTICE** | 第三方声明文件，通常包含版权和许可信息 |
| **SBOM** | 软件物料清单 |
| **SPDX** | 许可证/ SBOM 标准，也定义 SPDX License List |
| **CycloneDX** | 轻量 SBOM 标准，JSON/XML |
| **PURL** | Package URL，组件的统一标识 |
| **CPE** | Common Platform Enumeration，资产/漏洞标识 |
| **SCA** | Software Composition Analysis，软件组成分析 |
| **Dual Licensing** | 双许可，同一软件可选择不同许可证（如 Qt 开源/商业） |
| **Patent Grant** | 专利授权条款，如 Apache-2.0 中的专利许可 |
| **Tivoization** | 通过硬件锁定阻止用户替换软件，GPLv3/LGPLv3 对此有约束 |

---

## 12. 参考资料

- GNU GPL 常见问题：<https://www.gnu.org/licenses/gpl-faq.html>
- GNU GPLv2：<https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
- GNU GPLv3：<https://www.gnu.org/licenses/gpl-3.0.html>
- GNU LGPLv3：<https://www.gnu.org/licenses/lgpl-3.0.html>
- GNU AGPLv3：<https://www.gnu.org/licenses/agpl-3.0.html>
- Apache License 2.0：<https://www.apache.org/licenses/LICENSE-2.0>
- MIT License：<https://opensource.org/license/mit>
- BSD 3-Clause：<https://opensource.org/license/bsd-3-clause>
- Boost Software License：<https://www.boost.org/LICENSE_1_0.txt>
- zlib License：<https://opensource.org/license/zlib>
- Qt 开源许可义务：<https://www.qt.io/licensing/open-source-lgpl-obligations>
- OpenCASCADE 许可：<https://dev.opencascade.org/resources/licensing>
- Ghostscript 许可：<https://www.ghostscript.com/licensing/>
- libdxfrw：<https://github.com/LibreCAD/libdxfrw>
- Poppler：<https://poppler.freedesktop.org/>
- CycloneDX：<https://cyclonedx.org/>
- SPDX：<https://spdx.dev/>
- ScanCode Toolkit：<https://github.com/aboutcode-org/scancode-toolkit>
- OSS Review Toolkit：<https://oss-review-toolkit.org/>
- OpenSSF：<https://openssf.org/>

---

## 13. 变更记录

| 日期 | 变更 | 作者 |
|------|------|------|
| 2026-10-21 | 初版：许可证基础、当前依赖盘点、SBOM 与整改路线 | 待补充 |
