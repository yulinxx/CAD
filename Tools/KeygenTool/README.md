# KeygenTool — 离线注册码生成器

SanYiCAD License 模块的配套工具,用于为指定机器生成 RSA 签名的离线注册码。

## 工作原理

客户端 License 模块内嵌一把 RSA 公钥(混淆后硬编码在 `Licensing/License/Src/LicenseManager.cpp` 的 `kPublicKey` 中),KeygenTool 持有配套私钥对注册信息做 SHA-256 + RSA 签名。客户端用内嵌公钥验签,通过则激活。

因此:**KeygenTool 签出来的注册码,只有内嵌对应公钥的客户端才能验过**。换了密钥对必须把新公钥重新硬编码进客户端并重新编译,否则客户端一律返回 "Invalid registration code"。

```
机器码 + 到期日 + 功能 + 签发日 + 客户名
              │
              ▼
         组成 payload (pipe 拼接)
              │
              ▼
   RSA 私钥签名 (SHA-256 + PKCS#1 v1.5)
              │
              ▼
   Base64url(payload) . Base64url(签名)
              │
              ▼
         最终注册码字符串
```

## 构建

KeygenTool 不会随主程序分发给最终用户,也不链接 `License` DLL,而是直接把 keygen 源文件编进可执行文件(避免链接不导出的内部符号)。

### 集成构建(主工程 cmake)

```powershell
cmake -S . -B build -DBUILD_KEYGEN_TOOL=ON
cmake --build build --config Release --target KeygenTool
```

产物在 `build/bin_Qt6/<Config>/KeygenTool.exe`。

### 独立构建(本目录单独 cmake)

```powershell
cmake -S Tools/KeygenTool -B Tools/KeygenTool/build
cmake --build Tools/KeygenTool/build --config Release
```

## 使用

> **⚠️ PowerShell 用户请注意**:PowerShell 默认不从当前目录找可执行文件,**所有命令必须加 `.\` 前缀**,例如 `.\KeygenTool.exe genkey ...`。不加前缀会直接报 `CommandNotFoundException`。CMD 用户可省略 `.\`。

### 第一步:生成密钥对(首次使用才需要,终身只需一次)

```powershell
.\KeygenTool.exe genkey private.pem public.pem
```

- `private.pem` — **私钥,务必妥善保管,不要发给任何人**。丢了就再也签不出有效注册码。推荐备份到加密 U 盘或密码管理器。
- `public.pem` — 公钥。**必须把这把公钥内嵌到客户端二进制**,否则客户端验不过任何注册码。

### 第二步:给某台机器生成注册码

```powershell
.\KeygenTool.exe genreg <machine_code> <expiry_date> <features> <issue_date> <customer> <private.pem>
```

6 个参数:

| 参数 | 格式 | 示例 | 说明 |
| --- | --- | --- | --- |
| `machine_code` | 十六进制字符串 | `6b4f8af71e...` | 目标机器的机器码,从客户端「关于 / 授权」界面导出 |
| `expiry_date` | `YYYY-MM-DD` | `2027-12-31` | 到期日期 |
| `features` | 逗号分隔或 `all` | `all` / `2d,3d,nest` | 授权的功能集合 |
| `issue_date` | `YYYY-MM-DD` | `2026-10-21` | 签发日期,通常填当天 |
| `customer` | 字符串 | `张三` / `AcmeCorp` | 客户名称,写入 payload 可识别来源 |
| `private.pem` | 文件路径 | `private.pem` | 第一步生成的私钥文件 |

完整示例:

```powershell
.\KeygenTool.exe genreg 6b4f8af71e012731c9320e01dd4c3833eef73fab 2027-12-31 all 2026-10-21 "张三" private.pem
```

成功后输出:

```
Registration code:
NmI0ZjhhZjcxZTAxMjc...Ogz8m9kAdRgQ
```

复制整行注册码给客户,让他在客户端激活窗口粘贴即可。

## 新密钥对内嵌客户端(公钥替换)

当你首次生成密钥对或需要轮换密钥时,必须把新 `public.pem` 硬编码进 `LicenseManager.cpp`,否则客户端仍用旧公钥验签,所有注册码都会失败。

### 1. 运行混淆脚本

混淆算法是 XOR:`data[0]` 作为密钥,后续字节 = 明文 XOR `(key + i) & 0xFF`。在 PowerShell 中执行:

```powershell
$pub = Get-Content "public.pem" -Raw
$bytes = [System.Text.Encoding]::UTF8.GetBytes($pub)
$key   = 0x67   # 任选 1..255,与现有风格保持一致
$out   = @($key)
for ($i = 0; $i -lt $bytes.Length; $i++) {
    $out += $bytes[$i] -bxor (($key + $i) -band 0xFF)
}
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("    DEFINE_ENCRYPTED_STR(kPublicKey,")
for ($i = 0; $i -lt $out.Count; $i++) {
    if ($i % 12 -eq 0) { [void]$sb.Append("`n        ") }
    [void]$sb.Append(("0x{0:X2}, " -f $out[$i]))
}
[void]$sb.Append("`n    )")
$sb.ToString()
```

脚本会打印一个完整的 `DEFINE_ENCRYPTED_STR(kPublicKey, ...)` C++ 块。

### 2. 替换源码

打开 `Licensing/License/Src/LicenseManager.cpp`,文件顶部的 `namespace { ... }` 里有三个 `DEFINE_ENCRYPTED_STR`,把第一个(从 `DEFINE_ENCRYPTED_STR(kPublicKey,` 到对应右括号 `)`)整个替换为脚本输出的新块。

注意:**只替换 kPublicKey 这一个,下面的 kErrNoLicense 和 kErrExpired 不要动**。

### 3. 重新编译并部署客户端

```powershell
cmake --build build --config Release --target License SanYiCAD
```

常见坑:

- **LNK1104:无法打开 License.dll** — SanYiCAD.exe 还在运行,占着 DLL。先完全关闭 CAD 再编译。
- **客户端激活仍失败** — 检查是不是用了旧公钥编译的旧 DLL 被部署到了运行目录。确认 `build/bin_Qt6/Release/License.dll` 的修改时间是刚才。

## 错误码参考

KeygenTool `genreg` 返回值对应 `LicenseResult` 枚举:

| 值 | 含义 | 排查 |
| --- | --- | --- |
| 0 | 成功 | — |
| -1 | 参数无效 | 有空字符串或路径 |
| -5 | IO 错误 | **通常是 private.pem 不存在或路径错**,注意 PowerShell 下相对路径基于当前目录 |
| -99 | 内部异常 | OpenSSL 抛异常被 catch 住 |

客户端激活失败(提示 "Invalid registration code"):

| 原因 | 怎么验证 |
| --- | --- |
| 注册码对应的公钥没内嵌 | 把 public.pem 重新嵌入 LicenseManager.cpp 并重编 |
| 机器码不匹配 | 核对客户给的机器码与激活窗口显示的是否完全一致,大小写都要一致 |
| Payload 字段顺序错 | KeygenTool 和客户端用同一套 `machine\|expiry\|features\|issue\|customer` 顺序,源码里改了一边必须改另一边 |
| 签名算法不一致 | 客户端用 SHA-256 + RSA,如果 KeygenTool 改了算法必须同步 |

## 安全注意事项

KeygenTool **是开发/运维内部工具,严禁随主程序或安装包分发给最终用户**。它的存在意味着只要拿到私钥就能无限生成有效注册码,一旦泄漏授权体系等同虚设。

### 1. 构建层面隔离

- **默认不构建**:根 `Config.cmake` 里 `BUILD_KEYGEN_TOOL` 默认值是 **OFF**,只有显式传 `-DBUILD_KEYGEN_TOOL=ON` 才会进主工程。日常日常开发跑 `cmake -S . -B build` **不会生成 KeygenTool.exe**。
- **CPack 安装包不包含**:根 `CMakeLists.txt` 没有 `install(TARGETS KeygenTool)`,即使你在本地开了开关编出来,打包 `cpack` 时也不会被带进 NSIS/MSI 安装包。
- **License DLL 已排除**:`Licensing/License/CMakeLists.txt` 用 `list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/KeygenTool\\.cpp$")` 把 keygen 源码从发货 DLL 里剔除,客户端二进制**不可能**因链接 License 而意外带出签名能力。

### 2. 源码物理隔离

- **不要放进任何分发包**:不要把 `Tools/KeygenTool/` 目录、`private.pem`、`public.pem`、包含签名逻辑的 `KeygenTool.cpp` / `LicenseKeygenDLL.cpp` 打进 `dist/`、`release/`、压缩包、Git bundle 等交付物。
- **CI/CD 里跳过**:任何面向外部交付的流水线(如 GitHub Actions 打 tag 出包)里,`cmake` 必须带 `-DBUILD_KEYGEN_TOOL=OFF`,确保 CI 产物里不会意外出现 KeygenTool.exe。
- **源码审计**:每次发版前跑一遍 `grep -r "KeygenTool" dist/` 和 `grep -r "LicenseKeygen" dist/`,确认签名相关符号没被带进发行目录。

### 3. 私钥保管(最高优先级)

- **本地加密存储**:`private.pem` 是整个授权体系的根。**必须**用密码管理器(1Password / Bitwarden)加密保存,或放在 LUKS / BitLocker 加密卷里,**绝对不要**明文放在仓库目录、桌面、同步云盘里。
- **Git 历史清扫**:`.gitignore` **必须**排除 `*.pem`。**已提交过的私钥就算后来删了,也永远留在 git 历史里**。如果私钥曾被 commit 过,必须:
  1. 用 `git filter-repo` 或 BFG Repo-Cleaner 清除历史
  2. **立刻生成新密钥对**(旧私钥已泄漏,必须轮换)
  3. 新公钥重新硬编码进客户端、重新发版
- **备份分散**:密钥对至少两份备份,存在不同介质(加密 U 盘 + 离线密码管理器)。
- **员工离职即轮换**:任何有权访问私钥的人员离开团队,**立即生成新密钥对并同步到客户端**。

### 4. 私钥泄漏后的应急轮换

怀疑私钥被泄漏(包括误提交、邮件发送、U盘丢失):

```powershell
# 1) 本地新生成一对
.\KeygenTool.exe genkey private_new.pem public_new.pem

# 2) 把 public_new.pem 混淆后替换 LicenseManager.cpp 的 kPublicKey
#    (见上方「公钥内嵌客户端」一节的 PowerShell 脚本)

# 3) 重编并发布新版客户端
cmake --build build --config Release --target License SanYiCAD

# 4) 用新私钥重新给所有已激活客户签发注册码(旧注册码在新版客户端里会失效)

# 5) 旧 private.pem 物理删除(多途径:回收站 + 磁盘擦除)
```

### 5. 客户端端防篡改(已做的)

即便客户端二进制被反编译,攻击者也**拿不到**签名能力,因为:

- 客户端只嵌**公钥**,不嵌私钥。拿到公钥只能**验签**,不能**签名**。
- 公钥在 `LicenseManager.cpp` 里以 **XOR 混淆后硬编码**(`DEFINE_ENCRYPTED_STR` + `SECURE_SECTION`),不是裸 PEM 文本,常规 strings/binary grep 搜不到 `BEGIN PUBLIC KEY`。混淆只增加逆向成本,真正的护城河是**公私钥分离**。
- `KeygenTool.cpp` / `LicenseKeygenDLL.cpp` 不编进 License DLL,攻击者即使拿到完整源码仓,也只能在自己的开发环境里构建,发货路径上根本没有签名相关的对象文件。

### 一句话总结

> **私钥 = 印钞机。谁拿着它,谁就能无限生成有效注册码。保管好它,并确保它永远不会出现在最终用户能拿到的任何介质上。**
