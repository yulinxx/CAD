#!/usr/bin/env python3
# 代码 CJK 检查：剥离注释后不允许出现中日韩字符。
#
# 规则：注释用中文，代码与日志用英文。标识符、字符串字面量、日志文本、
# 断言消息中的 CJK 都会被拦截；注释中的 CJK 是允许的。
#
# 用法：
#   Tools/check-cjk-in-code.py --staged       检查已暂存文件（pre-commit 调用）
#   Tools/check-cjk-in-code.py --all          检查本仓库 git 跟踪的全部代码文件
#   Tools/check-cjk-in-code.py <路径>...      检查指定文件或目录（子模块需显式传入）
#
# 退出码：0 无违规，1 发现违规，2 用法错误。

import os
import subprocess
import sys

# 按扩展名归类注释语法
C_STYLE_EXTS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".inl", ".ipp", ".m", ".mm", ".js", ".jsx", ".qml",
}
HASH_EXTS = {".py", ".sh", ".bash", ".zsh", ".command"}
CMAKE_EXTS = {".cmake"}
PS_EXTS = {".ps1"}

# 目录级排除：构建产物、第三方、文档、生成中间目录
EXCLUDE_DIR_NAMES = {
    "build", ".git", "ThirdParty", "Docs", "node_modules",
    "CMakeFiles", "bin", "obj", ".cache", "cmake-build-debug", "cmake-build-release",
}

# 生成文件按文件名排除
EXCLUDE_FILE_PREFIXES = ("moc_", "ui_", "qrc_")
EXCLUDE_FILE_SUFFIXES = ("_generated.h", "_generated.cpp", ".pb.h", ".pb.cc", ".g.h", ".g.cpp")

# 路径级豁免：按项目决定保持现状，不报告
EXCLUDE_PATH_PREFIXES = ("Tools/",)
EXCLUDE_PATH_EXACT = ("gitManager.py",)

# 翻译数据白名单：这些脚本里的中文就是翻译数据本身
DATA_SCRIPT_WHITELIST = {
    "UI/Common/Translations/translation_data_new.py",
    "UI/Common/Translations/update_zh.py",
    "UI/Common/Translations/zh_bulk.py",
    "UI/Common/Translations/translation_data_props.py",
    "Python/scripts/update_zh_ts.py",
}

# 显式例外登记：(相对路径, 行内必须包含的内容)；内容为 None 表示整文件豁免。
# 只登记项目明确要求保留的数据性中文，新增例外必须在这里写清楚原因。
EXCEPTIONS = (
    # 真实字体族名，改了会导致字体匹配失败
    ("Engine/Common/Src/Text/FontManager.cpp", "宋体"),
    ("Engine/Common/Src/Text/FontManager.cpp", "新宋体"),
    ("Engine/Common/Src/Text/FontManager.cpp", "黑体"),
    ("Engine/Common/Src/Text/FontManager.cpp", "微软雅黑"),
    ("Engine/Common/Src/Text/FontManager.cpp", "楷体"),
    ("Engine/Common/Src/Text/FontManager.cpp", "仿宋"),
    # 语言自称，供语言选择器显示
    ("UI/Common/Include/UI/LanguageManager.h", "中文"),
    ("UI/Common/Include/UI/LanguageManager.h", "日本語"),
    ("UI/Common/Include/UI/LanguageManager.h", "한국어"),
    # 材料别名，兼容中文输入
    ("Engraving/Engraving/Src/LaserStrategy.cpp", "木材"),
    ("Engraving/Engraving/Src/LaserStrategy.cpp", "金属"),
    ("Engraving/Engraving/Src/LaserStrategy.cpp", "亚克力"),
    ("Engraving/Engraving/Src/LaserStrategy.cpp", "皮革"),
    ("Engraving/Engraving/Src/LaserStrategy.cpp", "玻璃"),
    # 中文输入样张与输入法测试数据
    ("UI/2D/Src/UI/Dlg/TextInputDlg.cpp", None),
    ("UI/2D/Src/UI/Dlg/QRCodeInputDlg.cpp", None),
    # 示例程序里的中文示例路径/示例输出，非产品代码
    ("Utility/Example/main.cpp", None),
    ("GeoModelCore/Example/main.cpp", None),
    # 系统菜单标题随语言变化，用于匹配 macOS 注入的 Edit 菜单
    ("Main/Src/Platform/MacMenuCleanup.mm", "@\"编辑\""),
    ("Main/Src/Platform/MacMenuCleanup.mm", "@\"編集\""),
    # 字体渲染预览样张（中/日/韩）
    ("UI/Common/Src/Dlg/FontSettingsTab.cpp", "采菊东篱下"),
    # 生成代码里的中文注释（写入生成文件的模板文本）
    ("Renderx/CMake/EmbedShaders.cmake", "自动生成"),
    ("Renderx/CMake/EmbedShaders.cmake", "shader 请改"),
    ("Renderx/CMake/EmbedShaders.cmake", "文件已声明为构建依赖"),
)


def is_cjk(ch):
    # 覆盖 Han、Hiragana、Katakana、Hangul 及扩展区
    o = ord(ch)
    return (
        0x2E80 <= o <= 0x2FDF
        or o in (0x3005, 0x3007)
        or 0x3040 <= o <= 0x30FF
        or 0x31F0 <= o <= 0x31FF
        or 0x3400 <= o <= 0x4DBF
        or 0x4E00 <= o <= 0x9FFF
        or 0xF900 <= o <= 0xFAFF
        or 0x1100 <= o <= 0x11FF
        or 0x3130 <= o <= 0x318F
        or 0xAC00 <= o <= 0xD7AF
        or 0x20000 <= o <= 0x2FA1F
        or 0x30000 <= o <= 0x3134F
    )


def mask_c_like(text):
    # 用等长空格替换注释字符，保留换行以对齐行号；字符串内容保持原样。
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    quote = ""

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        ch = text[i]
        if state == "code":
            if ch == "/" and i + 1 < n and text[i + 1] == "/":
                state = "line"
                blank(i, i + 2)
                i += 2
                continue
            if ch == "/" and i + 1 < n and text[i + 1] == "*":
                state = "block"
                blank(i, i + 2)
                i += 2
                continue
            # C++ 原始字符串 R"delim( ... )delim"
            if ch == "R" and i + 1 < n and text[i + 1] == '"':
                j = i + 2
                while j < n and text[j] not in "(\n" and j - (i + 2) <= 16:
                    j += 1
                if j < n and text[j] == "(":
                    end_token = ")" + text[i + 2:j] + '"'
                    k = text.find(end_token, j + 1)
                    i = n if k < 0 else k + len(end_token)
                    continue
                i += 1
                continue
            if ch in "\"'":
                state = "string"
                quote = ch
                i += 1
                continue
            i += 1
        elif state == "line":
            if ch == "\n":
                # 行尾反斜杠表示注释续行
                if i > 0 and text[i - 1] == "\\":
                    i += 1
                    continue
                state = "code"
                i += 1
                continue
            out[i] = " "
            i += 1
        elif state == "block":
            if ch == "*" and i + 1 < n and text[i + 1] == "/":
                blank(i, i + 2)
                state = "code"
                i += 2
                continue
            if ch != "\n":
                out[i] = " "
            i += 1
        else:  # string
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                state = "code"
            elif ch == "\n":
                # 未闭合的普通字符串不跨行，回到代码态避免整文件失效
                state = "code"
            i += 1
    return "".join(out)


def mask_hash_like(text, triple_quotes=False, bracket_comment=False):
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    quote = ""

    while i < n:
        ch = text[i]
        if state == "code":
            if bracket_comment and text.startswith("#[[", i):
                end = text.find("]]", i + 3)
                stop = n if end < 0 else end + 2
                for k in range(i, stop):
                    if out[k] != "\n":
                        out[k] = " "
                i = stop
                continue
            if ch == "#":
                prev = text[i - 1] if i > 0 else "\n"
                # 仅行首或空白后的 # 视为注释，避免 ${#var}、a#b 误判
                if prev in " \t\n":
                    k = i
                    while k < n and text[k] != "\n":
                        out[k] = " "
                        k += 1
                    i = k
                    continue
                i += 1
                continue
            if triple_quotes and (text.startswith('"""', i) or text.startswith("'''", i)):
                q = text[i:i + 3]
                k = i + 3
                while k < n:
                    if text[k] == "\\":
                        k += 2
                        continue
                    if text.startswith(q, k):
                        k += 3
                        break
                    k += 1
                i = k
                continue
            if ch in "\"'":
                state = "string"
                quote = ch
                i += 1
                continue
            i += 1
        else:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                state = "code"
            elif ch == "\n":
                state = "code"
            i += 1
    return "".join(out)


def mask_ps(text):
    # PowerShell：<# #> 块注释、# 行注释，字符串 '...' 与 "..."（反引号转义）
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    quote = ""

    while i < n:
        ch = text[i]
        if state == "code":
            if text.startswith("<#", i):
                end = text.find("#>", i + 2)
                stop = n if end < 0 else end + 2
                for k in range(i, stop):
                    if out[k] != "\n":
                        out[k] = " "
                i = stop
                continue
            if ch == "#":
                k = i
                while k < n and text[k] != "\n":
                    out[k] = " "
                    k += 1
                i = k
                continue
            if ch in "\"'":
                state = "string"
                quote = ch
                i += 1
                continue
            i += 1
        else:
            if ch == "`":
                i += 2
                continue
            if ch == quote:
                # PowerShell 中 '' 与 "" 为转义，双写时跳过
                if i + 1 < n and text[i + 1] == quote:
                    i += 2
                    continue
                state = "code"
            elif ch == "\n":
                state = "code"
            i += 1
    return "".join(out)


def shebang_style(data):
    first = data.split(b"\n", 1)[0]
    if not first.startswith(b"#!"):
        return None
    line = first.decode("utf-8", "replace").lower()
    if "python" in line:
        return "hash_py"
    if any(s in line for s in ("bash", "/sh", "zsh", "dash")):
        return "hash"
    if "pwsh" in line or "powershell" in line:
        return "ps"
    return None


def style_for(path, data):
    name = os.path.basename(path)
    ext = os.path.splitext(name)[1].lower()
    if name == "CMakeLists.txt" or ext in CMAKE_EXTS:
        return "cmake"
    if ext in C_STYLE_EXTS:
        return "c"
    if ext in HASH_EXTS:
        return "hash_py" if ext == ".py" else "hash"
    if ext in PS_EXTS:
        return "ps"
    return shebang_style(data)


def excluded(path):
    parts = path.replace("\\", "/").split("/")
    for part in parts[:-1]:
        if part in EXCLUDE_DIR_NAMES or part.startswith("build-") or part.startswith("bin_"):
            return True
    name = parts[-1]
    if name.startswith(EXCLUDE_FILE_PREFIXES):
        return True
    if name.endswith(EXCLUDE_FILE_SUFFIXES):
        return True
    return False


def decode(data):
    if b"\x00" in data[:8192]:
        return None
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        text = data.decode("utf-8", "replace")
    # 去掉 BOM，否则文件首行的 # 会被当成普通字符，注释识别失效
    if text.startswith("\ufeff"):
        text = text[1:]
    return text


def is_excepted(path, content):
    # 显式例外：只豁免登记过的 (文件, 内容) 组合；内容为 None 表示整文件豁免。
    for rule_path, needle in EXCEPTIONS:
        if path != rule_path:
            continue
        if needle is None or needle in content:
            return True
    return False


def path_excluded(rel):
    # 按项目决定整体保持现状的路径：Tools/ 与 gitManager.py
    if rel in EXCLUDE_PATH_EXACT:
        return True
    return any(rel.startswith(prefix) for prefix in EXCLUDE_PATH_PREFIXES)


def scan_content(path, text, style):
    if style == "c":
        masked = mask_c_like(text)
    elif style == "cmake":
        masked = mask_hash_like(text, triple_quotes=False, bracket_comment=True)
    elif style == "hash_py":
        masked = mask_hash_like(text, triple_quotes=True)
    elif style == "hash":
        masked = mask_hash_like(text, triple_quotes=False)
    elif style == "ps":
        masked = mask_ps(text)
    else:
        return []

    original_lines = text.splitlines()
    violations = []
    for idx, masked_line in enumerate(masked.splitlines()):
        if not any(is_cjk(ch) for ch in masked_line):
            continue
        content = original_lines[idx].strip() if idx < len(original_lines) else ""
        if is_excepted(path, content):
            continue
        if len(content) > 160:
            content = content[:160] + "..."
        violations.append((idx + 1, content))
    return violations


def emit(path, violations):
    for line_no, content in violations:
        print("{}:{}: {}".format(path, line_no, content))


def run_git(args):
    return subprocess.run(["git"] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def git_root():
    proc = run_git(["rev-parse", "--show-toplevel"])
    if proc.returncode != 0:
        sys.stderr.write("error: not inside a git repository\n")
        sys.exit(2)
    return proc.stdout.decode("utf-8", "replace").strip()


def staged_paths(root):
    proc = run_git(["diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"])
    if proc.returncode != 0:
        sys.stderr.write("error: git diff --cached failed\n")
        sys.exit(2)
    raw = proc.stdout.decode("utf-8", "replace")
    return [p for p in raw.split("\x00") if p]


def tracked_paths(root):
    proc = run_git(["ls-files", "-z"])
    if proc.returncode != 0:
        sys.stderr.write("error: git ls-files failed\n")
        sys.exit(2)
    raw = proc.stdout.decode("utf-8", "replace")
    return [p for p in raw.split("\x00") if p]


def walk_paths(paths):
    result = []
    for path in paths:
        if os.path.isdir(path):
            for dirpath, dirnames, filenames in os.walk(path):
                dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIR_NAMES
                               and not d.startswith("build-") and not d.startswith("bin_")]
                for name in filenames:
                    result.append(os.path.join(dirpath, name))
        elif os.path.isfile(path):
            result.append(path)
        else:
            sys.stderr.write("warning: skip missing path: {}\n".format(path))
    return result


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write(
            "usage: check-cjk-in-code.py --staged | --all | <path>...\n"
            "checks CJK outside comments (comments may be Chinese)\n"
        )
        return 0 if argv else 2

    root = git_root()
    staged = False
    candidates = []

    if argv[0] == "--staged":
        staged = True
        candidates = staged_paths(root)
    elif argv[0] == "--all":
        candidates = tracked_paths(root)
    else:
        candidates = walk_paths(argv)

    scanned = 0
    files_with_violations = 0
    total = 0

    for path in candidates:
        rel = os.path.relpath(path, root) if os.path.isabs(path) else path
        if excluded(rel):
            continue
        # 项目决定保持现状的路径，不参与检查也不报告
        if path_excluded(rel):
            continue
        # 翻译数据脚本：中文就是数据本身
        if rel in DATA_SCRIPT_WHITELIST:
            continue
        # --all 会把子模块 gitlink 也列出来，跳到目录即可排除
        if os.path.isdir(path):
            continue
        if staged:
            proc = run_git(["show", ":" + rel])
            if proc.returncode != 0:
                continue
            data = proc.stdout
        else:
            if not os.path.isfile(path):
                continue
            with open(path, "rb") as handle:
                data = handle.read()
        text = decode(data)
        if text is None:
            continue
        style = style_for(rel, data)
        if style is None:
            continue
        scanned += 1
        violations = scan_content(rel, text, style)
        if violations:
            files_with_violations += 1
            total += len(violations)
            emit(rel, violations)

    print("")
    print("CJK check: {} violation(s) in {} file(s), {} file(s) scanned".format(
        total, files_with_violations, scanned))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
