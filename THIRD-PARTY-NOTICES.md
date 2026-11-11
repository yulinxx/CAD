# 第三方组件许可声明（Third-Party Notices）

本文件随 April CAD 一起分发，用于声明产品中包含的第三方开源组件及其许可条款。
产品自身的许可条款见 [LICENSE.md](LICENSE.md)。

本文件至少覆盖存在许可义务或合规风险的组件；完整的第三方依赖清单见
[ThirdParty/README.md](ThirdParty/README.md)，机器可读的组件清单见
[Docs/07-工程与合规/sbom/april-sbom.cdx.json](Docs/07-工程与合规/sbom/april-sbom.cdx.json)。

---

## 1. libdxfrw

| 项 | 内容 |
|---|---|
| 组件名称 | libdxfrw |
| 版本 | 2.0.0 |
| 上游仓库 | https://github.com/LibreCAD/libdxfrw |
| 许可证 | **GPL-2.0-or-later**（GNU General Public License v2.0 或更高版本） |
| 许可证全文 | 随包提供于 `licenses/libdxfrw/COPYING`，仓库内为 `ThirdParty/libdxfrw/COPYING` |
| 使用方式 | 以源码形式编译进 `FileIO.dll`，用于 DXF 文件的解析与写出 |
| 修改状态 | **已修改**。相对上游 `d68fb1ed937ad2a74a54f6208277ec10a1b031b0` 有 5 个本地提交及未提交改动 |
| 修改记录 | 见仓库内 `Docs/07-工程与合规/libdxfrw-modifications/` |
| 源代码获取 | 见本文件第 3 节 |

### 许可义务说明

libdxfrw 以 GPLv2（或更高版本）发布。GPLv2 要求：分发包含该组件的二进制程序时，须同时以
GPLv2 提供完整的对应源代码，并保留版权声明与许可证声明。本文件随包提供许可证全文与修改记录；
源代码获取方式见第 3 节。

本工程对 libdxfrw 的使用形态（编入 `FileIO.dll` 与闭源主程序同进程运行）的合规结论，
以及后续处理方案，需由法务最终确认。

---

## 2. 其他第三方组件

产品还包含通过 vcpkg 引入的第三方库（如 Qt 6、Boost、OpenCASCADE、OpenCV、protobuf、
Clipper2、SQLiteCpp、NanoSVG、stb、WebP、TIFF 等）及其各自的许可证。
各组件来源、版本与许可证条目见 [ThirdParty/README.md](ThirdParty/README.md) 与
`Docs/07-工程与合规/sbom/april-sbom.cdx.json`。

---

## 3. 源代码获取方式

如需获取本产品中包含的 GPL 组件的对应源代码（含本地修改），可通过以下方式联系：

- 邮箱：support@april-cad.com
- 地址：https://www.april-cad.com/support

我们将在收到请求后按 GPLv2 第 3 条的要求提供对应源代码。
