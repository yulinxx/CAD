#!/usr/bin/env python3
"""
crash_analyze.py - 跨平台 Breakpad minidump 崩溃分析器（增强版）

功能概述：
  直接解析由 Crashpad / Breakpad 在 Windows / Linux / macOS 上生成的 *.dmp
  崩溃转储文件，无需完整 Breakpad 工具链即可提取核心崩溃信息，并尝试进行
  符号化回溯（symbolicated backtrace）。

输出内容：
  1. 顶部崩溃摘要（异常原因、故障地址、崩溃 PC、系统平台、时间）
  2. Minidump 文件头信息（版本、Stream 数量、各 Stream 偏移）
  3. 系统信息（OS、CPU 架构、核心数、构建版本）
  4. 加载模块列表（基址、大小、时间戳、名称/路径）
  5. 异常 / 崩溃原因（异常码、原因描述、故障地址、异常参数）
  6. 崩溃线程完整寄存器上下文（PC/SP/FP/LR + 所有通用寄存器）
  7. 堆栈内存 Hex Dump（SP 附近内存，含 ASCII 视图）
  8. 符号化回溯（优先调用 minidump_stackwalk，否则内置 frame-pointer walker）
  9. 其他线程概览（PC 及所在模块）

用法示例：
  python crash_analyze.py /path/to/crash.dmp
  python crash_analyze.py crash.dmp --search-dir build/bin/Debug
  python crash_analyze.py crash.dmp --dump-stackwalk /opt/breakpad/minidump_stackwalk
  python crash_analyze.py crash.dmp --raw        # 仅解析，不符号化
  python crash_analyze.py crash.dmp --no-color   # 禁用彩色输出

退出码：
  0  解析成功
  1  文件无法解析（不是有效的 minidump）
  2  解析成功，但崩溃 PC 无法映射到任何已加载模块
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ===========================================================================
# 常量定义（来源于 google_breakpad 的 minidump_format.h）
# ===========================================================================

# Minidump 文件签名，小端序下内存中显示为 "MDMP"
# 注意：原代码注释误写为 PMDM，实际应为 MDMP
MD_HEADER_SIGNATURE = 0x504D444D

# Breakpad 标准 Stream 类型编号
MD_THREAD_LIST_STREAM = 3   # 线程列表（包含每个线程的栈基址、寄存器上下文位置）
MD_MODULE_LIST_STREAM = 4   # 加载模块列表（可执行文件、动态库）
MD_MEMORY_LIST_STREAM = 5   # 捕获的内存区域列表（通常包含各线程栈内存）
MD_EXCEPTION_STREAM   = 6   # 异常信息（崩溃原因、故障地址、崩溃线程 ID）
MD_SYSTEM_INFO_STREAM = 7   # 系统信息（OS、CPU 架构、处理器数量等）

# 上下文标志（用于识别寄存器上下文格式，但跨平台时不可靠，仅作参考）
MD_CONTEXT_AMD64 = 0x00100000
MD_CONTEXT_ARM64 = 0x00400000

# Stream 类型编号 -> 人类可读名称映射表
STREAM_NAMES = {
    3: "ThreadList", 4: "ModuleList", 5: "MemoryList", 6: "Exception",
    7: "SystemInfo",
}

# ===========================================================================
# 终端颜色控制（自动检测 TTY，支持 --no-color 禁用）
# ===========================================================================

class Colors:
    """ANSI 颜色码封装类。可通过 Colors.enabled(False) 全局禁用颜色输出，
    方便将结果重定向到文件或在不支持 ANSI 的终端中使用。"""
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    RESET = '\033[0m'

    @classmethod
    def enabled(cls, enable: bool = True) -> None:
        """开启或关闭所有颜色输出。当 enable=False 时，将所有颜色属性设为空字符串。"""
        if not enable:
            for attr in ('RED', 'GREEN', 'YELLOW', 'BLUE', 'MAGENTA',
                         'CYAN', 'WHITE', 'BOLD', 'DIM', 'RESET'):
                setattr(cls, attr, "")

# ===========================================================================
# 小型数据容器（使用 @dataclass 减少样板代码）
# ===========================================================================

@dataclass
class ModuleEntry:
    """表示一个已加载的模块（可执行文件或动态库）。
    
    字段说明：
      name:      模块的短名称（通常来自 CodeView 记录，如 libFoo.so）
      base:      模块在进程地址空间中的加载基址（ImageBase）
      size:      模块在内存中的映像大小（SizeOfImage）
      path:      完整路径（如果 dump 中记录了的话），否则与 name 相同
      timestamp: 模块的 PE/ELF 时间戳（用于匹配符号文件）
      checksum:  模块校验和
    """
    name: str
    base: int
    size: int
    path: str
    timestamp: int
    checksum: int


@dataclass
class ThreadEntry:
    """表示 minidump 中记录的一个线程。
    
    字段说明：
      thread_id:    操作系统线程 ID（TID）
      stack_base:   该线程栈的基地址（用于在 MemoryList 中定位栈内存）
      context_rva:  该线程寄存器上下文（CONTEXT 结构）在 dump 文件中的 RVA
      context_size: 寄存器上下文数据的大小（字节）
    """
    thread_id: int
    stack_base: int
    context_rva: int
    context_size: int


@dataclass
class RegisterSet:
    """统一表示不同架构的寄存器集合，方便后续处理逻辑与架构无关。
    
    字段说明：
      pc:    程序计数器（AMD64=RIP, ARM64=PC, X86=EIP）
      sp:    栈指针（AMD64=RSP, ARM64=SP, X86=ESP）
      fp:    帧指针（AMD64=RBP, ARM64=X29, X86=EBP），可能为 None
      lr:    链接寄存器（ARM64=X30），在 AMD64 中复用为存储 RIP 以兼容 frame-1 逻辑
      arch:  架构标识字符串（"amd64" / "arm64" / "x86"）
      extra: 其余通用寄存器的字典，如 {"rax": ..., "x0": ..., "eax": ...}
    """
    pc: int
    sp: int
    fp: Optional[int]
    lr: int
    arch: str
    extra: Dict[str, int] = field(default_factory=dict)

# ===========================================================================
# Minidump 结构解析（核心二进制解析层）
# ===========================================================================

def read_mdstring(buf: bytes, rva: int) -> str:
    """从 dump 文件中读取一个 MINIDUMP_STRING 结构。
    
    格式：前 4 字节为小端序 u32 表示字节数（含 UTF-16LE 编码），后面紧跟字符数据。
    如果 rva 为 0 表示该字段未设置，返回空字符串。
    """
    if rva == 0 or rva + 4 > len(buf):
        return ""
    length = struct.unpack_from("<I", buf, rva)[0]      # 字节数
    if length == 0 or length > 4096 or rva + 4 + length > len(buf):
        return ""
    raw = buf[rva + 4:rva + 4 + length]
    try:
        s = raw.decode("utf-16-le", errors="replace")
    except Exception:
        s = raw.decode("latin-1", errors="replace")
    # 在第一个 null 字符处截断
    npos = s.find("\x00")
    if npos >= 0:
        s = s[:npos]
    return s.strip()


def parse_minidump(path: str):
    """解析 minidump 文件头及 Stream 目录。
    
    返回值：
      buf:       整个文件的原始字节内容
      header:    包含 version、stream_count、checksum、timestamp、flags 的字典
      directory: Stream 类型 -> (数据大小, RVA) 的字典
    
    异常：
      文件小于 32 字节或签名不匹配时抛出 ValueError。
    """
    with open(path, "rb") as f:
        buf = f.read()
    if len(buf) < 32:
        raise ValueError("文件过小，不是有效的 minidump")
    
    # Minidump 文件头结构（小端序）：
    #   sig(4) + version(4) + stream_count(4) + dir_rva(4) + checksum(4) + timestamp(4) + flags(8)
    # 注意：flags 在 64 位 dump 中为 8 字节（Q），Breakpad 通常生成此格式。
    sig, version, stream_count, dir_rva, checksum, timestamp, flags = \
        struct.unpack_from("<IIIIIIQ", buf, 0)
    
    if sig != MD_HEADER_SIGNATURE:
        raise ValueError("不是有效的 minidump（签名错误 0x%08x）" % sig)

    header = {
        "version": version,
        "stream_count": stream_count,
        "checksum": checksum,
        "timestamp": timestamp,
        "flags": flags,
    }

    # Stream 目录（Directory）是一个 MDRawDirectory 数组。
    # 每个目录项 = stream_type(u32) + data_size(u32) + rva(u32) = 12 字节。
    directory: Dict[int, Tuple[int, int]] = {}
    for i in range(stream_count):
        b = dir_rva + i * 12
        stream_type, data_size, rva = struct.unpack_from("<III", buf, b)
        directory[stream_type] = (data_size, rva)

    return buf, header, directory


def read_system_info(buf: bytes, loc: Tuple[int, int]) -> dict:
    """读取 SYSTEM_INFO Stream（类型 7）。
    
    返回包含 OS、CPU 架构、版本号、处理器数量、补丁版本（CSD）等信息的字典。
    """
    size, rva = loc
    # MDRawSystemInfo 结构布局（小端序）：
    #   processor_arch(u16) + processor_level(u16) + processor_revision(u16)
    #   number_of_processors(u8) + product_type(u8)
    #   major_version(u32) + minor_version(u32) + build_number(u32)
    #   platform_id(u32) + csd_version_rva(u32) + ...
    arch = struct.unpack_from("<H", buf, rva + 0)[0]
    nproc = buf[rva + 6]
    major = struct.unpack_from("<I", buf, rva + 8)[0]
    minor = struct.unpack_from("<I", buf, rva + 12)[0]
    build = struct.unpack_from("<I", buf, rva + 16)[0]
    platform = struct.unpack_from("<I", buf, rva + 20)[0]
    csd_rva = struct.unpack_from("<I", buf, rva + 24)[0]
    
    # 架构编号映射（Breakpad 扩展了一些非标准值如 0x8003 等）
    arch_names = {
        0: "x86", 5: "arm", 9: "amd64", 12: "arm64",
        0x8001: "sparc", 0x8002: "ppc64",
        0x8003: "arm64-old", 0x8004: "mips64",
        0x8005: "riscv", 0x8006: "riscv64",
    }
    # OS 平台编号映射
    os_names = {
        0: "Win32s", 1: "Win32Windows", 2: "Win32NT",
        0x8000: "Unix", 0x8101: "MacOS", 0x8102: "iOS",
        0x8201: "Linux", 0x8203: "Android"
    }
    return {
        "arch": arch_names.get(arch, "0x%x" % arch),
        "os": os_names.get(platform, "0x%x" % platform),
        "major": major, "minor": minor, "build": build,
        "nproc": nproc,
        "csd": read_mdstring(buf, csd_rva),  # CSD = "Service Pack" 或构建版本字符串
    }


def read_thread_list(buf: bytes, loc: Tuple[int, int]) -> List[ThreadEntry]:
    """读取 THREAD_LIST Stream（类型 3），提取所有线程的基本信息。
    
    MDRawThreadList 结构：
      number_of_threads(u32) + 4 字节对齐填充（64 位 dump）
      后面紧跟 number_of_threads 个 MDRawThread 记录（每个 48 字节）。
    
    MDRawThread 关键字段（偏移）：
      +0  thread_id(u32)
      +24 stack.start_of_memory_range(u64)  -> 栈基址
      +40 context.data_size(u32)
      +44 context.rva(u32)                  -> 寄存器上下文位置
    """
    size, rva = loc
    count = struct.unpack_from("<I", buf, rva)[0]
    threads = []
    rec = 48      # 每个 MDRawThread 记录固定 48 字节
    header = 8    # count(u32) + 4 字节对齐填充
    for i in range(count):
        b = rva + header + i * rec
        if b + 48 > len(buf):
            break
        tid = struct.unpack_from("<I", buf, b + 0)[0]
        stack_base = struct.unpack_from("<Q", buf, b + 24)[0]
        ctx_size = struct.unpack_from("<I", buf, b + 40)[0]
        ctx_rva = struct.unpack_from("<I", buf, b + 44)[0]
        threads.append(ThreadEntry(tid, stack_base, ctx_rva, ctx_size))
    return threads


def cv_record_name(buf: bytes, cv_rva: int, cv_size: int) -> str:
    """从 MDRawModule 的 cv_record（CodeView 记录）中提取模块名称。
    
    现代 Crashpad / Breakpad 在 macOS 上通常将模块短名称存放在 CodeView 记录中
    （RSDS/PDB70 格式：4 字节签名 + 16 字节 GUID + 4 字节 age + 0 结尾 ASCII 名称），
    而不是放在 module_name_rva 指向的 MINIDUMP_STRING 中。
    
    支持的签名：
      RSDS  -> PDB 7.0 格式（最常见）
      NB07  -> PDB 2.0 格式
      NB10  -> CodeView NB10
      MCS7  -> Breakpad 旧版 macOS 专用格式
    """
    if not cv_rva or not cv_size:
        return ""
    data = buf[cv_rva:cv_rva + cv_size]
    if len(data) < 4:
        return ""
    sig = data[:4]
    if sig in (b"RSDS", b"NB07", b"NB10"):
        # 签名(4) + GUID(16) + age(4) = 24，后面是 0 结尾的 ASCII 名称
        name = data[24:].split(b"", 1)[0]
        try:
            return name.decode("utf-8", errors="replace")
        except Exception:
            return ""
    if sig == b"MCS7":  # Breakpad 旧版 macOS CodeView 记录
        name = data[4:].split(b"", 1)[0]
        try:
            return name.decode("utf-8", errors="replace")
        except Exception:
            return ""
    return ""


def cv_record_name_raw(data: bytes) -> str:
    """从内联 CodeView 数据中提取模块名称（Breakpad 格式）。
    
    数据格式：RSDS/NB07/NB10 签名(4) + GUID(16) + age(4) + 0 结尾 ASCII 名称。
    """
    if len(data) < 24:
        return ""
    sig = data[:4]
    if sig in (b"RSDS", b"NB07", b"NB10"):
        name = data[24:].split(b"\x00", 1)[0]
        try:
            return name.decode("utf-8", errors="replace")
        except Exception:
            return ""
    return ""


def _is_plausible_base(v: int) -> bool:
    """判断一个地址是否像是合法的模块加载基址。
    
    经验规则：
      - 必须大于等于 0x100000000（4GB 以上，现代 64 位系统常见）
      - 必须小于等于 0x7FFFFFFFFFFF（用户空间上限）
      - 必须按页对齐（低 12 位为 0）
    这些规则用于在自适应 stride 探测时筛选合法的 base 地址。
    """
    return 0x100000000 <= v <= 0x7FFFFFFFFFFF and (v & 0xFFF) == 0


def read_module_list(buf: bytes, loc: Tuple[int, int]) -> List[ModuleEntry]:
    """读取 MODULE_LIST Stream（类型 4），提取所有已加载模块。
    
    关键难点：不同平台 / 不同版本的 Breakpad/Crashpad 生成的 MDRawModule 记录长度（stride）
    可能不同（56=Microsoft, 100=Breakpad, 108 等）。因此采用自适应探测：
      1. 尝试多个候选 header 大小（4 或 8 字节）和 stride
      2. 检查每个记录的起始 8 字节（base address）是否合法
      3. 选择能验证全部记录且得分最高的组合
    
    字段布局（在选定 stride 内的固定偏移）：
      +0   base_of_image(u64)
      +8   size_of_image(u32)
      +12  checksum(u32)
      +16  time_date_stamp(u32)
      +20  module_name_rva(u32)   -> MINIDUMP_STRING（完整路径）
      +24  cv_record(u32*2 或 52字节内联) -> CodeView 记录（通常含短名称）
    """
    size, rva = loc
    count = struct.unpack_from("<I", buf, rva)[0]
    if count == 0:
        return []

    # 自适应 stride 探测（同时尝试 hdr=4 和 hdr=8，覆盖有无对齐填充的情况）
    best_stride, best_score, best_hdr = -1, -1, 4
    for hdr in (4, 8):
        for stride in (56, 100, 108, 112, 116, 120, 128, 136, 144, 160, 200, 216, 224):
            score = 0
            checked = 0
            for i in range(count):
                b = rva + hdr + i * stride
                if b + 16 > len(buf):
                    break
                checked += 1
                base = struct.unpack_from("<Q", buf, b)[0]
                if base == 0 or _is_plausible_base(base):
                    score += 1
                else:
                    break
            if checked == count and score > best_score:
                best_stride, best_score, best_hdr = stride, score, hdr
    if best_stride < 0:
        return []

    mods: List[ModuleEntry] = []
    hdr = best_hdr
    for i in range(count):
        b = rva + hdr + i * best_stride
        # 修复：使用 best_stride 做边界检查，而非硬编码 108
        if b + best_stride > len(buf):
            break

        base = struct.unpack_from("<Q", buf, b + 0)[0]
        img_size = struct.unpack_from("<I", buf, b + 8)[0]
        checksum = struct.unpack_from("<I", buf, b + 12)[0]
        tds = struct.unpack_from("<I", buf, b + 16)[0]

        # 从 module_name_rva 读取完整路径（旧版 Breakpad / Linux dump 常用）
        name_rva = struct.unpack_from("<I", buf, b + 20)[0]
        name_from_rva = read_mdstring(buf, name_rva) if name_rva + 4 <= len(buf) else ""

        # 从 cv_record 读取短名称（新版 Crashpad / macOS 常用）
        # cv_record 在 module_name_rva(+20, 4字节) 之后，偏移 +24
        # 对于 Microsoft 格式 (stride=56)：cv_record 是 MINIDUMP_LOCATION_DATA (data_size+rva)
        # 对于 Breakpad 格式 (stride>=100)：cv_record 是内联 CodeView 数据 (RSDS等)
        name_from_cv = ""
        if best_stride >= 56:
            cv_sig = struct.unpack_from("<I", buf, b + 24)[0]
            if cv_sig == 0x53445352 or cv_sig == 0x3037424e or cv_sig == 0x3031424e:
                # Breakpad 格式：内联 CodeView 数据 (RSDS/NB07/NB10)
                cv_data = buf[b + 24:b + 24 + min(52, best_stride - 24)]
                name_from_cv = cv_record_name_raw(cv_data)
            else:
                # Microsoft 格式：MINIDUMP_LOCATION_DATA -> 跟随 rva
                cv_size = struct.unpack_from("<I", buf, b + 24)[0]
                cv_rva = struct.unpack_from("<I", buf, b + 28)[0]
                if cv_size and cv_rva and cv_rva + cv_size <= len(buf):
                    name_from_cv = cv_record_name(buf, cv_rva, cv_size)

        # 名称优先级：cv_record 短名 > module_name_rva 的 basename > 十六进制占位符
        if name_from_cv:
            name = name_from_cv
            path = name_from_rva if name_from_rva else name
        elif name_from_rva:
            name = os.path.basename(name_from_rva)
            path = name_from_rva
        else:
            name = hex(base) if _is_plausible_base(base) else "???"
            path = name

        # 过滤掉 base 为 0 或不合法的占位符记录
        if base == 0 or not _is_plausible_base(base):
            continue
        mods.append(ModuleEntry(name, base, img_size, path, tds, checksum))
    return mods


def read_exception(buf: bytes, loc: Tuple[int, int]) -> dict:
    """读取 EXCEPTION Stream（类型 6），提取崩溃异常信息。
    
    MDRawExceptionStream 结构关键字段：
      +0   thread_id(u32)         -> 崩溃线程
      +8   exception_code(u32)    -> 平台相关的异常码
      +12  exception_flags(u32)
      +24  exception_address(u64) -> 触发异常的地址（故障地址）
      +32  number_of_parameters(u32)
      +40  exception_information[15](u64 数组) -> 异常附加参数
      +160 thread_context.size(u32) -> 崩溃线程寄存器上下文的位置和大小
      +164 thread_context.rva(u32)
    
    注意：早期代码将 ctx_size 和 ctx_rva 的顺序读反了，导致切出垃圾数据。
    这里已修正为 size 在前、rva 在后。
    """
    size, rva = loc
    tid = struct.unpack_from("<I", buf, rva + 0)[0]
    code = struct.unpack_from("<I", buf, rva + 8)[0]
    flags = struct.unpack_from("<I", buf, rva + 12)[0]
    addr = struct.unpack_from("<Q", buf, rva + 24)[0]
    nparam = struct.unpack_from("<I", buf, rva + 32)[0]
    params = list(struct.unpack_from("%dQ" % nparam, buf, rva + 40))[0:nparam]
    ctx_size = struct.unpack_from("<I", buf, rva + 160)[0]
    ctx_rva = struct.unpack_from("<I", buf, rva + 164)[0]
    reason = describe_exception(code)
    return {
        "code": code, "reason": reason, "address": addr,
        "thread_id": tid, "ctx_rva": ctx_rva, "ctx_size": ctx_size,
        "params": params, "flags": flags
    }


def describe_exception(code: int) -> str:
    """将平台相关的异常码转换为人类可读的描述字符串。
    
    覆盖范围：
      - macOS Mach 异常（EXC_BAD_ACCESS、EXC_BREAKPOINT、EXC_CRASH 等）
      - Windows SEH 异常码（ACCESS_VIOLATION、STACK_OVERFLOW、INTEGER_DIVIDE_BY_ZERO 等）
      - Linux/Unix 信号（SIGILL、SIGSEGV、SIGBUS、SIGABRT 等）
    """
    mac = {
        1: "EXC_BAD_ACCESS (无效地址)",
        2: "EXC_BAD_ACCESS (保护失败)",
        3: "EXC_BREAKPOINT / EXC_ARM_DA_ALIGN",
        4: "EXC_ILLEGAL_INSTRUCTION (非法指令)",
        5: "EXC_ARITHMETIC (算术异常)",
        6: "EXC_EMULATION",
        7: "EXC_SOFTWARE",
        8: "EXC_SYSCALL",
        9: "EXC_MACH_SYSCALL",
        10: "EXC_RPC_ALERT",
        11: "EXC_CRASH (SIGABRT 等)",
        0x00010000: "SIGSYS (非法系统调用)",
        0x00010001: "SIGPIPE (管道破裂)",
        0x00010002: "SIGPROF (性能分析定时器)",
        0x00010003: "SIGKILL (被强制终止)",
        0x00010004: "SIGBUS (总线错误)",
        0x00010005: "SIGSEGV (段错误)",
    }
    win = {
        0xC0000005: "ACCESS_VIOLATION (访问冲突)",
        0xC00000FD: "STACK_OVERFLOW (栈溢出)",
        0xC0000094: "INTEGER_DIVIDE_BY_ZERO (整数除零)",
        0xC0000096: "PRIVILEGED_INSTRUCTION (特权指令)",
        0xC000001D: "ILLEGAL_INSTRUCTION (非法指令)",
        0xC0000006: "IN_PAGE_ERROR (页面错误)",
        0x80000003: "BREAKPOINT (断点)",
        0x80000002: "DATATYPE_MISALIGNMENT (数据未对齐)",
        0xC000008C: "ARRAY_BOUNDS_EXCEEDED (数组越界)",
        0xC000008D: "FLOAT_DENORMAL_OPERAND (浮点非正规数)",
        0xC000008E: "FLOAT_DIVIDE_BY_ZERO (浮点除零)",
        0xC000008F: "FLOAT_INEXACT_RESULT (浮点不精确结果)",
    }
    linux = {
        0x00000004: "SIGILL (非法指令)",
        0x00000005: "SIGTRAP (断点/陷阱)",
        0x00000006: "SIGABRT (异常终止)",
        0x00000007: "SIGBUS (总线错误)",
        0x00000008: "SIGFPE (浮点异常)",
        0x0000000B: "SIGSEGV (段错误)",
        0x0000000C: "SIGSYS (非法系统调用)",
    }
    if code in mac:
        return mac[code]
    if code in win:
        return win[code]
    if code in linux:
        return linux[code]
    if code == 0x00000000:
        return "EXC_BAD_ACCESS / 未知异常"
    return "0x%08x" % code

# ===========================================================================
# 寄存器上下文解析（按架构分发）
# ===========================================================================

def parse_amd64_context(data: bytes) -> Optional[RegisterSet]:
    """解析 AMD64 (x86-64) 的 MDRawContextAMD64 结构。
    
    Breakpad 的 MDRawContextAMD64 布局：
      p1home..p6home      : 48 字节 (6 x u64)
      context_flags       : 4 字节
      mxcsr               : 4 字节
      seg_cs, seg_ds, seg_es, seg_fs, seg_gs, seg_ss : 12 字节
      eflags              : 4 字节
      dr0..dr7            : 64 字节 (8 x u64)
      --- 以上合计 120 字节，之后是 GPR 块 ---
      rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8..r15 : 16 x u64 = 128 字节
      rip                 : 在 GPR 块中偏移 16*8 = 128，即总偏移 248
    
    某些旧版或简化 dump 可能省略了 segregs+eflags，导致 GPR 从 104 开始。
    这里以标准 120 为默认值，但如果标准布局读出的 RIP 为 0 而 104 布局能读出非零值，
    则自动回退到 104 布局。
    """
    if len(data) < 256:
        return None

    gpr = 120  # 标准 Breakpad 布局
    # 用标准布局读取 RIP 做 sanity check
    rip_std = struct.unpack_from("<Q", data, gpr + 16 * 8)[0] if len(data) >= gpr + 136 else 0
    if rip_std == 0 and len(data) >= 240:
        # 尝试旧版布局（无 segregs+eflags）
        rip_alt = struct.unpack_from("<Q", data, 104 + 16 * 8)[0]
        if rip_alt != 0:
            gpr = 104

    try:
        rsp = struct.unpack_from("<Q", data, gpr + 4 * 8)[0]   # RSP 是第 5 个 GPR
        rbp = struct.unpack_from("<Q", data, gpr + 5 * 8)[0]   # RBP 是第 6 个
        rip = struct.unpack_from("<Q", data, gpr + 16 * 8)[0]  # RIP 是第 17 个
    except struct.error:
        return None

    names = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
    extra = {}
    for i, n in enumerate(names):
        if n == "rip":
            continue
        try:
            extra[n] = struct.unpack_from("<Q", data, gpr + i * 8)[0]
        except struct.error:
            pass
    return RegisterSet(rip, rsp, rbp if rbp else None, rip, "amd64", extra)


def parse_arm64_context(data: bytes) -> Optional[RegisterSet]:
    """解析 ARM64 (AArch64) 的 MDRawContextARM64 结构。
    
    布局：
      context_flags : 4 字节
      cpsr          : 4 字节
      iregs[33]     : 33 x u64 = 264 字节（x0..x31 + pc）
      总计约 272 字节
    
    寄存器映射：
      x[0..31]  : 通用寄存器 X0-X31
      x[32]     : PC（程序计数器）
      x[33-2]=x[31] : SP（栈指针，即 X31/WSP）
      x[29]     : FP（帧指针）
      x[30]     : LR（链接寄存器）
    """
    if len(data) < 272:
        return None
    off = 8  # 跳过 context_flags + cpsr
    x = [struct.unpack_from("<Q", data, off + i * 8)[0] for i in range(33)]
    extra = {f"x{i}": x[i] for i in range(32)}
    return RegisterSet(
        pc=x[32],
        sp=x[33 - 2],      # X31 在 Breakpad 中作为 SP
        fp=x[29] if x[29] else None,
        lr=x[30],
        arch="arm64",
        extra=extra
    )


def parse_x86_context(data: bytes) -> Optional[RegisterSet]:
    """解析 X86 (32-bit) 的 MDRawContextX86 结构。
    
    标准 Windows CONTEXT 布局（Breakpad 遵循此布局）：
      +0    context_flags
      +4    dr0..dr3, dr6, dr7          : 32 字节
      +36   float_save (80 字节，但 Breakpad 可能简化)
      +56   segregs (gs, fs, es, ds)    : 16 字节（但 Breakpad 中通常 12 字节）
      实际 Breakpad MDRawContextX86 中整数寄存器块起始约 72：
        +72  edi, esi, ebx, edx, ecx, eax  (6 x u32)
        +96  ebp
        +100 eip
        +104 cs, eflags
        +112 esp
        +116 ss
    
    这里采用 Breakpad 源码中确认的布局偏移。
    """
    if len(data) < 124:
        return None
    try:
        eip = struct.unpack_from("<I", data, 100)[0]
        esp = struct.unpack_from("<I", data, 112)[0]
        ebp = struct.unpack_from("<I", data, 96)[0]
    except struct.error:
        return None
    extra_names = [("edi", 72), ("esi", 76), ("ebx", 80), ("edx", 84),
                   ("ecx", 88), ("eax", 92)]
    extra = {n: struct.unpack_from("<I", data, off)[0] for n, off in extra_names}
    return RegisterSet(eip, esp, ebp if ebp else None, eip, "x86", extra)


def parse_context(data: bytes, arch: str) -> Optional[RegisterSet]:
    """根据架构标识分派到对应的寄存器解析函数。
    
    重要：不能仅依赖 context_flags 字段来判断架构，因为 macOS Crashpad 在 ARM64
    上写入的 context_flags 是 0x80000006，与 Breakpad 标准的 MD_CONTEXT_ARM64
    (0x00400000) 不一致。因此使用从 SYSTEM_INFO Stream 中读出的 arch 字段来分派。
    """
    if data is None or len(data) < 4:
        return None
    if arch == "amd64":
        return parse_amd64_context(data)
    if arch in ("arm64", "arm64-old"):
        return parse_arm64_context(data)
    if arch == "x86":
        return parse_x86_context(data)
    return None


# ===========================================================================
# 符号化层（将原始地址转换为函数名+文件行号）
# ===========================================================================

def find_module_for_pc(mods: List[ModuleEntry], pc: int) -> Optional[ModuleEntry]:
    """根据程序计数器 PC 查找其所属的加载模块。
    
    判断逻辑：模块基址 <= PC < 模块基址 + max(模块大小, 1)。
    使用 max(size, 1) 防止 size 为 0 时无法匹配（某些 dump 中 size 字段不可靠）。
    """
    for m in mods:
        end = m.base + max(m.size, 1)
        if m.base <= pc < end:
            return m
    return None


def best_symbolizer() -> Optional[str]:
    """在系统 PATH 中查找可用的符号化工具，按优先级返回第一个找到的：
      1. atos      -> macOS 自带，支持 dSYM
      2. addr2line -> GNU binutils，跨平台
      3. llvm-symbolizer -> LLVM 项目，支持 DWARF 和 PDB
    """
    for t in ["atos", "addr2line", "llvm-symbolizer"]:
        if shutil.which(t):
            return t
    return None


def candidate_binaries(mod: ModuleEntry, search_dirs: List[str]) -> List[str]:
    """为给定模块生成候选二进制文件路径列表。
    
    搜索策略：
      1. 如果 dump 中记录的 mod.path 存在且是文件，直接加入候选列表。
      2. 在 --search-dir 指定的目录及其子目录中递归搜索：
         - 精确匹配模块 basename（如 libFoo.so.1.2.3）
         - 前缀严格匹配（如 libFoo.so 匹配 libFoo.so.1.2.3，但不匹配 libFooBar.so）
    """
    out: List[str] = []
    if mod.path and os.path.isfile(mod.path):
        out.append(mod.path)
    base = os.path.basename(mod.name)
    prefix = base.split(".")[0] if "." in base else base
    for d in search_dirs:
        if not d or not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for fn in files:
                if fn == base:
                    cand = os.path.join(root, fn)
                    if cand not in out:
                        out.append(cand)
                elif prefix and (fn.startswith(prefix + ".") or fn.startswith(prefix + "_")):
                    # 严格前缀匹配：避免 libFoo 错误匹配 libFooBar
                    cand = os.path.join(root, fn)
                    if cand not in out:
                        out.append(cand)
    return out


def symbolicate(pc: int, mod: ModuleEntry, binary: str, loader: str) -> Optional[str]:
    """调用外部符号化工具，将绝对地址 PC 解析为函数名+源文件行号。
    
    各工具调用方式：
      - atos: 需要原始加载基址（-l）和绝对地址，输出格式较友好。
      - llvm-symbolizer: 标准用法为传入相对基址的地址（0x<rel>）作为独立参数。
      - addr2line: 传入相对基址的地址，使用 -f（显示函数名）和 -C（C++  demangle）。
    
    修复记录：
      - 原代码 addr2line 使用了错误的 @ 前缀（@0x... 会被当成文件名而非地址），
        已修正为直接传递十六进制字符串。
      - 原代码 llvm-symbolizer 使用了不存在的 --addresses 和 --fbase 参数，
        已修正为标准 CLI。
    """
    rel = pc - mod.base  # 模块内相对偏移（大多数工具需要相对地址）
    if loader == "atos":
        # atos 需要绝对地址和加载基址
        cmd = ["atos", "-o", binary, "-l", hex(mod.base), hex(pc)]
    elif loader == "llvm-symbolizer":
        cmd = ["llvm-symbolizer", "--obj=" + binary, "0x%x" % rel]
    else:  # addr2line（GNU binutils）
        cmd = [loader, "-e", binary, "-f", "-C", "0x%x" % rel]
    
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    except Exception:
        return None
    if out.returncode != 0 or not out.stdout.strip():
        return None
    
    lines = [ln.strip() for ln in out.stdout.splitlines() if ln.strip()]
    if not lines:
        return None

    # addr2line -f -C 输出两行：第一行是函数名（已 demangle），第二行是 file:line
    if loader == "addr2line" and len(lines) >= 2:
        func = lines[0]
        loc = lines[1]
        if loc in ("??:0", "??:?", "??"):
            return func
        return f"{func} at {loc}"

    # llvm-symbolizer 输出类似：函数名 \n file:line（可能有多组 inlined 帧）
    if loader == "llvm-symbolizer" and len(lines) >= 2:
        func = lines[0]
        loc = lines[1]
        if "??:0" in loc or "??:?" in loc:
            return func
        return f"{func} at {loc}"

    # atos 输出单行，如 "main (in MyApp) (main.cpp:42)"，直接返回
    return "; ".join(lines)


def read_thread_stack_memory(buf: bytes, directory: dict, t: ThreadEntry):
    """从 MEMORY_LIST Stream 中读取指定线程的栈内存数据。
    
    MEMORY_LIST Stream 包含一组 MDMemoryDescriptor：
      start_of_range(u64) + data_size(u32) + rva(u32) = 16 字节/条
    
    通过比较 ThreadEntry.stack_base 与每个内存区域的起始地址，找到包含栈的
    内存区域，返回 (内存字节数据, 区域起始地址)。
    """
    if MD_MEMORY_LIST_STREAM not in directory:
        return None, 0
    size, rva = directory[MD_MEMORY_LIST_STREAM]
    count = struct.unpack_from("<I", buf, rva)[0]
    for i in range(count):
        b = rva + 4 + i * 16
        start = struct.unpack_from("<Q", buf, b + 0)[0]
        mem_size = struct.unpack_from("<I", buf, b + 8)[0]
        mem_rva = struct.unpack_from("<I", buf, b + 12)[0]
        if start <= t.stack_base < start + mem_size:
            return buf[mem_rva:mem_rva + mem_size], start
    return None, 0


def frame_pointer_walk(reg: RegisterSet, crash_tid: int, threads: List[ThreadEntry],
                       mods: List[ModuleEntry], search_dirs: List[str],
                       buf: bytes, directory: dict,
                       max_frames: int = 16) -> List[Tuple[int, Optional[str]]]:
    """通过帧指针链（frame-pointer chain）遍历调用栈，获取崩溃线程的深层调用帧。
    
    工作原理（以 64 位为例）：
      栈上每个栈帧的布局为 [next_fp][return_addr][局部变量...]。
      当前 FP 指向 next_fp，FP+8 指向 return_addr。
      通过不断读取 next_fp 并跟随，直到 next_fp 为 0 或越界。
    
    修复：原代码硬编码 ptr_size=8，在 32 位 dump 中会地址错位。
    现在根据 reg.arch 动态选择 4 字节（x86）或 8 字节（amd64/arm64）。
    
    返回值：列表，每个元素为 (return_address, symbol_string_or_None)。
    """
    t = next((x for x in threads if x.thread_id == crash_tid), None)
    if not t or not reg.fp or args_check_no_raw():
        return []
    stack, base = read_thread_stack_memory(buf, directory, t)
    if not stack:
        return []

    # 根据架构选择指针宽度：64 位用 8 字节，32 位用 4 字节
    ptr_size = 8 if reg.arch in ("amd64", "arm64", "arm64-old") else 4
    ptr_fmt = "<Q" if ptr_size == 8 else "<I"

    loader = best_symbolizer()
    frames: List[Tuple[int, Optional[str]]] = []
    fp = reg.fp
    seen = set()  # 防止循环引用导致无限循环

    def read_ptr(addr: int) -> int:
        """从捕获的栈内存中读取一个指针（考虑基址偏移）。"""
        off = addr - base
        if 0 <= off < len(stack) - ptr_size + 1:
            return struct.unpack_from(ptr_fmt, stack, off)[0]
        return 0

    for _ in range(max_frames):
        if fp in seen:
            break
        # 确保 FP 在有效栈内存范围内，且至少能容纳两个指针（next_fp + ret_addr）
        if not (base <= fp < base + len(stack) - 2 * ptr_size + 1):
            break
        seen.add(fp)
        next_fp = read_ptr(fp)
        ret_addr = read_ptr(fp + ptr_size)
        if ret_addr == 0:
            break
        
        # 尝试符号化该返回地址
        mod = find_module_for_pc(mods, ret_addr)
        resolved = None
        if mod and loader:
            for c in candidate_binaries(mod, search_dirs):
                resolved = symbolicate(ret_addr, mod, c, loader)
                if resolved:
                    break
        frames.append((ret_addr, resolved))
        if next_fp == 0:
            break
        fp = next_fp
    return frames


# 模块级标志，供 frame_pointer_walk 在无参情况下判断 --raw 模式
_NO_RAW_WALK = False


def args_check_no_raw() -> bool:
    """检查当前是否为 --raw 模式（跳过符号化）。"""
    return _NO_RAW_WALK

# ===========================================================================
# 输出格式化层（将解析结果以人类友好的方式打印）
# ===========================================================================

def banner(title: str) -> None:
    """打印一个带颜色的分隔横幅，用于划分报告的不同章节。"""
    print(f"\n{Colors.CYAN}{Colors.BOLD}{'-'*70}{Colors.RESET}")
    print(f"{Colors.CYAN}{Colors.BOLD}  {title}{Colors.RESET}")
    print(f"{Colors.CYAN}{Colors.BOLD}{'-'*70}{Colors.RESET}")


def dump_registers(rs: RegisterSet) -> None:
    """以双列表格形式打印寄存器集合，核心寄存器置顶显示。"""
    print(f"  {Colors.BOLD}架构:{Colors.RESET} {rs.arch}")
    print(f"  {'寄存器':<6} {'值':<20} {'寄存器':<6} {'值':<20}")
    print(f"  {'-'*6} {'-'*20} {'-'*6} {'-'*20}")
    core = [("PC", rs.pc), ("SP", rs.sp)]
    if rs.fp is not None:
        core.append(("FP", rs.fp))
    core.append(("LR", rs.lr))
    all_regs = core + sorted(rs.extra.items())
    for i in range(0, len(all_regs), 2):
        r1, v1 = all_regs[i]
        v1_s = f"0x{v1:016x}" if isinstance(v1, int) else "N/A"
        if i + 1 < len(all_regs):
            r2, v2 = all_regs[i + 1]
            v2_s = f"0x{v2:016x}" if isinstance(v2, int) else "N/A"
            print(f"  {r1:<6} {v1_s:<20} {r2:<6} {v2_s:<20}")
        else:
            print(f"  {r1:<6} {v1_s:<20}")


def dump_modules(mods: List[ModuleEntry]) -> None:
    """打印加载模块列表，包含基址、大小、时间戳和名称/路径。"""
    banner("加载模块列表")
    print(f"  {'基址':<18} {'大小':<10} {'时间戳':<12} {'名称'}")
    print(f"  {'-'*18} {'-'*10} {'-'*12} {'-'*40}")
    for m in mods:
        ts_str = time.strftime("%Y-%m-%d", time.gmtime(m.timestamp)) if m.timestamp else "-"
        name_disp = m.name if len(m.name) <= 40 else m.name[:37] + "..."
        line = f"  0x{m.base:016x} {m.size:<10} {ts_str:<12} {name_disp}"
        try:
            print(line)
        except UnicodeEncodeError:
            print(line.encode("ascii", errors="replace").decode("ascii"))
        if m.path and m.path != m.name and len(m.path) > len(m.name):
            path_disp = m.path if len(m.path) <= 58 else "..." + m.path[-55:]
            line2 = f"  {'':18} {'':10} {'':12} {Colors.DIM}{path_disp}{Colors.RESET}"
            try:
                print(line2)
            except UnicodeEncodeError:
                safe = path_disp.encode("ascii", errors="replace").decode("ascii")
                print(f"  {'':18} {'':10} {'':12} {safe}")


def dump_stack_memory(buf: bytes, directory: dict, t: ThreadEntry,
                      reg: RegisterSet, max_bytes: int = 256) -> None:
    """打印崩溃线程 SP 附近的堆栈内存 Hex Dump（含 ASCII 视图）。
    
    显示范围：SP 前后各 64 字节，加上 max_bytes 的向前扩展，
    帮助分析栈上的返回地址、局部变量和函数参数。
    """
    stack, base = read_thread_stack_memory(buf, directory, t)
    if not stack or not reg.sp:
        return
    sp_off = reg.sp - base
    start = max(0, sp_off - 64)          # 向后看 64 字节
    end = min(len(stack), sp_off + max_bytes)  # 向前看 max_bytes
    if start >= end:
        return

    print(f"\n  {Colors.BOLD}堆栈内存 (SP=0x{reg.sp:016x}, 捕获 {len(stack)} bytes):{Colors.RESET}")
    for row in range(start, end, 16):
        chunk = stack[row:row + 16]
        hex_str = " ".join(f"{b:02x}" for b in chunk)
        ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        addr = base + row
        # 在包含 SP 的行末尾标注 <-- SP
        marker = f" {Colors.GREEN}<-- SP{Colors.RESET}" if row <= sp_off < row + 16 else ""
        print(f"    0x{addr:016x}  {hex_str:<48}  {ascii_str}{marker}")


def dump_all_threads(threads: List[ThreadEntry], crash_tid: int, buf: bytes,
                     directory: dict, arch: str, mods: List[ModuleEntry]) -> None:
    """打印所有非崩溃线程的简要信息（PC 及所在模块），帮助判断是否是死锁或竞争条件。"""
    if len(threads) <= 1:
        return
    print(f"\n  {Colors.BOLD}其他线程 ({len(threads)-1} 个):{Colors.RESET}")
    for t in threads:
        if t.thread_id == crash_tid:
            continue
        reg = None
        if t.context_rva and t.context_size:
            data = buf[t.context_rva:t.context_rva + t.context_size]
            reg = parse_context(data, arch)
        if reg:
            mod = find_module_for_pc(mods, reg.pc)
            loc = f"PC=0x{reg.pc:016x}"
            if mod:
                loc += f" ({mod.name}+0x{reg.pc - mod.base:x})"
            print(f"    tid={t.thread_id:<6} stack=0x{t.stack_base:016x}  {loc}")
        else:
            print(f"    tid={t.thread_id:<6} stack=0x{t.stack_base:016x}  (无上下文)")


def print_summary(header: dict, sysinfo: Optional[dict], exc: Optional[dict],
                  crashed_reg: Optional[RegisterSet], mods: List[ModuleEntry],
                  crash_tid: int) -> None:
    """打印顶部崩溃摘要——这是运行脚本后第一眼看到的内容，用高亮框包裹，
    包含最关键的信息，方便快速定位问题。"""
    print(f"\n{Colors.BOLD}{'='*70}{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.RED}  崩溃摘要{Colors.RESET}")
    print(f"{Colors.BOLD}{'='*70}{Colors.RESET}")

    if exc:
        print(f"  {Colors.BOLD}异常原因:{Colors.RESET} {Colors.RED}{exc['reason']}{Colors.RESET}")
        print(f"  {Colors.BOLD}故障地址:{Colors.RESET} 0x{exc['address']:016x}")
        print(f"  {Colors.BOLD}崩溃线程:{Colors.RESET} {exc['thread_id']}")
        if exc.get("params"):
            print(f"  {Colors.BOLD}异常参数:{Colors.RESET}")
            for i, p in enumerate(exc["params"]):
                print(f"    [{i}] 0x{p:016x}")

    if crashed_reg:
        pc = crashed_reg.pc
        mod = find_module_for_pc(mods, pc)
        if mod:
            off = pc - mod.base
            print(f"  {Colors.BOLD}崩溃 PC:  {Colors.RESET} 0x{pc:016x} ({mod.name}+0x{off:x})")
        else:
            print(f"  {Colors.BOLD}崩溃 PC:  {Colors.RESET} 0x{pc:016x} ({Colors.YELLOW}未匹配模块{Colors.RESET})")

    if sysinfo:
        print(f"  {Colors.BOLD}系统平台:{Colors.RESET} {sysinfo['os']} {sysinfo['major']}.{sysinfo['minor']}.{sysinfo['build']} ({sysinfo['arch']})")

    ts = header.get("timestamp", 0)
    if ts:
        try:
            dt = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(ts))
            print(f"  {Colors.BOLD}崩溃时间:{Colors.RESET} {dt}")
        except Exception:
            pass

    print(f"{Colors.BOLD}{'='*70}{Colors.RESET}\n")


def show_backtrace(crashed_reg: Optional[RegisterSet], crash_tid: int,
                   threads: List[ThreadEntry], mods: List[ModuleEntry],
                   search_dirs: List[str], buf: bytes, directory: dict,
                   args) -> int:
    """打印符号化回溯（Backtrace）。
    
    策略优先级：
      1. 如果用户指定了 --dump-stackwalk 或系统 PATH 中存在 minidump_stackwalk，
         优先调用该外部工具（结果最完整，支持展开内联帧、walkstack 等）。
      2. 如果外部工具不可用或失败，回退到内置解析器：
         - Frame 0：崩溃 PC（可能从 LR 恢复，适用于 ARM64 null 间接调用）
         - Frame 1：LR（返回地址）
         - Frame 2+：通过 frame-pointer chain 遍历深层调用栈
    
    返回值：
      0  成功
      2  崩溃 PC 无法映射到任何模块（符号化可能不完整）
    """
    banner("BACKTRACE")

    # 策略 1：尝试外部 minidump_stackwalk
    sw = args.dump_stackwalk or shutil.which("minidump_stackwalk")
    if sw and not args.raw:
        print(f"{Colors.CYAN}[i] 尝试使用外部工具: {sw}{Colors.RESET}")
        cmd = [sw, args.dump]
        if args.symbols_dir:
            cmd += [args.symbols_dir]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if out.returncode == 0:
                for line in out.stdout.splitlines():
                    print("  " + line)
                return 0
            if out.stdout:
                print(out.stdout)
            if out.stderr:
                print(out.stderr, file=sys.stderr)
        except Exception as e:
            print(f"{Colors.RED}[!] minidump_stackwalk 失败: {e}{Colors.RESET}", file=sys.stderr)
        print(f"{Colors.YELLOW}[i] 回退到内置解析器{Colors.RESET}\n")

    # 如果没有捕获到寄存器上下文，无法做任何回溯
    if not crashed_reg:
        print(f"  {Colors.YELLOW}(未捕获线程上下文 - 无法回溯){Colors.RESET}")
        return 0

    loader = best_symbolizer()
    if loader:
        print(f"{Colors.CYAN}[i] 符号化工具: {loader}{Colors.RESET}")
    else:
        print(f"{Colors.YELLOW}[i] 未找到符号化工具 (atos / addr2line / llvm-symbolizer){Colors.RESET}")

    # ===== Frame 0：崩溃 PC =====
    pc = crashed_reg.pc
    pc_note = ""
    mod0 = find_module_for_pc(mods, pc)

    # ARM64 特殊处理：null 间接调用时 PC 可能为 0，真正的调用点保存在 LR 中
    if (not mod0 or pc == 0) and crashed_reg.lr and crashed_reg.arch in ("arm64", "arm64-old"):
        mod_lr = find_module_for_pc(mods, crashed_reg.lr)
        if mod_lr:
            pc, mod0, pc_note = crashed_reg.lr, mod_lr, (
                f" {Colors.YELLOW}(PC=0x0, 从 LR 恢复调用点){Colors.RESET}")

    if not mod0:
        print(f"  {Colors.RED}frame  0: pc=0x{crashed_reg.pc:016x} (未匹配到任何模块){Colors.RESET}")
        return 2

    # 尝试符号化 frame 0
    line0 = None
    for c in candidate_binaries(mod0, search_dirs):
        line0 = symbolicate(pc, mod0, c, loader)
        if line0:
            break
    offset0 = pc - mod0.base
    fallback0 = f"0x{pc:016x} {mod0.name}+0x{offset0:x}"
    sym0 = line0 or fallback0
    print(f"  {Colors.BOLD}{Colors.RED}frame  0: {sym0}{pc_note}{Colors.RESET}")

    # ===== Frame 1：LR（返回地址） =====
    # 如果 frame 0 已经从 LR 恢复，则跳过 frame 1，避免打印重复地址
    if crashed_reg.lr and crashed_reg.lr != crashed_reg.pc and not pc_note:
        mod1 = find_module_for_pc(mods, crashed_reg.lr)
        line1 = None
        if mod1:
            for c in candidate_binaries(mod1, search_dirs):
                line1 = symbolicate(crashed_reg.lr, mod1, c, loader)
                if line1:
                    break
            offset1 = crashed_reg.lr - mod1.base
            fallback1 = f"0x{crashed_reg.lr:016x} {mod1.name}+0x{offset1:x}"
        else:
            fallback1 = f"0x{crashed_reg.lr:016x} (未知模块)"
        sym1 = line1 or fallback1
        print(f"  {Colors.RED}frame  1: {sym1}{Colors.RESET}")

    # ===== Frame 2+：通过帧指针链遍历深层调用栈 =====
    if not args.raw:
        deeper = frame_pointer_walk(crashed_reg, crash_tid, threads, mods,
                                    search_dirs, buf, directory)
        for i, (addr, sym) in enumerate(deeper, start=2):
            if sym:
                print(f"  frame {i:2d}: {sym}")
            else:
                mod = find_module_for_pc(mods, addr)
                if mod:
                    off = addr - mod.base
                    print(f"  frame {i:2d}: 0x{addr:016x} {mod.name}+0x{off:x}")
                else:
                    print(f"  frame {i:2d}: 0x{addr:016x} (未知模块)")

        if not deeper and not crashed_reg.lr:
            print(f"  {Colors.DIM}(此 dump 中帧指针链不可用){Colors.RESET}")
    else:
        print(f"  {Colors.DIM}(--raw 模式，跳过深层帧解析){Colors.RESET}")

    print(f"\n  {Colors.DIM}[提示] 如需完整符号化回溯，请构建 Breakpad 的 `minidump_stackwalk` 并使用 --dump-stackwalk 指定路径。{Colors.RESET}")
    return 0


# ===========================================================================
# 主入口
# ===========================================================================

def main() -> int:
    """程序主入口。流程：
      1. 解析命令行参数
      2. 读取并解析 minidump 文件
      3. 按顺序解析各 Stream（SystemInfo、ModuleList、Exception、ThreadList）
      4. 提取崩溃线程的寄存器上下文
      5. 按优先级输出：摘要 -> 头信息 -> 系统信息 -> 模块列表 -> 异常信息 -> 
         寄存器 -> 堆栈内存 -> 回溯 -> 其他线程
      6. 返回退出码（0/1/2）
    """
    ap = argparse.ArgumentParser(
        description="跨平台 Breakpad minidump 崩溃分析器 (增强版)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python crash_analyze.py crash.dmp
  python crash_analyze.py crash.dmp --search-dir build/bin/Debug --search-dir build/lib
  python crash_analyze.py crash.dmp --dump-stackwalk /usr/local/bin/minidump_stackwalk
  python crash_analyze.py crash.dmp --raw
  python crash_analyze.py crash.dmp --no-color > report.txt
        """)
    ap.add_argument("dump", help="*.dmp 文件路径")
    ap.add_argument("--search-dir", action="append", default=[],
                    help="构建输出目录，用于查找调试二进制文件 (可多次指定)")
    ap.add_argument("--symbols-dir", default=None,
                    help="Breakpad .sym 符号文件目录 (用于 minidump_stackwalk)")
    ap.add_argument("--raw", action="store_true",
                    help="仅解析 dump 结构，跳过符号化")
    ap.add_argument("--dump-stackwalk", default=None,
                    help="外部 minidump_stackwalk 二进制路径")
    ap.add_argument("--no-color", action="store_true",
                    help="禁用彩色输出")
    args = ap.parse_args()

    # 设置全局 --raw 标志，供 frame_pointer_walk 在无参情况下读取
    global _NO_RAW_WALK
    _NO_RAW_WALK = args.raw
    # 自动检测终端是否支持颜色，或根据 --no-color 强制禁用
    Colors.enabled(not args.no_color and sys.stdout.isatty())

    # ------------------------------------------------------------------
    # 步骤 1：解析 minidump 文件头及 Stream 目录
    # ------------------------------------------------------------------
    try:
        buf, header, directory = parse_minidump(args.dump)
    except Exception as e:
        print(f"{Colors.RED}[!] {e}{Colors.RESET}", file=sys.stderr)
        return 1

    # ------------------------------------------------------------------
    # 步骤 2：按需解析各个 Stream
    # ------------------------------------------------------------------
    sysinfo: Optional[dict] = None
    if MD_SYSTEM_INFO_STREAM in directory:
        sysinfo = read_system_info(buf, directory[MD_SYSTEM_INFO_STREAM])

    mods: List[ModuleEntry] = []
    if MD_MODULE_LIST_STREAM in directory:
        mods = read_module_list(buf, directory[MD_MODULE_LIST_STREAM])

    exc: Optional[dict] = None
    if MD_EXCEPTION_STREAM in directory:
        exc = read_exception(buf, directory[MD_EXCEPTION_STREAM])

    threads: List[ThreadEntry] = []
    if MD_THREAD_LIST_STREAM in directory:
        threads = read_thread_list(buf, directory[MD_THREAD_LIST_STREAM])

    # 确定崩溃线程 ID：优先使用 Exception Stream 中的 thread_id，
    # 如果没有 Exception Stream，则回退到第一个线程的 ID
    crash_tid = exc["thread_id"] if exc else (
        threads[0].thread_id if threads else -1)

    # ------------------------------------------------------------------
    # 步骤 3：提取崩溃线程的寄存器上下文
    # ------------------------------------------------------------------
    crashed_reg: Optional[RegisterSet] = None
    crashed_arch = sysinfo["arch"] if sysinfo else "unknown"
    
    # 优先从 Exception Stream 的 thread_context 中获取（这是崩溃瞬间的精确上下文）
    if exc and exc.get("ctx_rva"):
        data = buf[exc["ctx_rva"]:exc["ctx_rva"] + exc["ctx_size"]]
        crashed_reg = parse_context(data, crashed_arch)
    # 如果 Exception Stream 中没有上下文，则从 ThreadList 中该线程的记录获取
    elif threads:
        t = next((x for x in threads if x.thread_id == crash_tid), None)
        if t and t.context_rva and t.context_size:
            data = buf[t.context_rva:t.context_rva + t.context_size]
            crashed_reg = parse_context(data, crashed_arch)

    # ------------------------------------------------------------------
    # 步骤 4：输出报告
    # ------------------------------------------------------------------
    
    # 4.1 顶部崩溃摘要（第一眼看到的关键信息）
    print_summary(header, sysinfo, exc, crashed_reg, mods, crash_tid)

    # 4.2 Minidump 文件头详情
    banner("MINIDUMP 头信息")
    print(f"  版本号           = 0x{header['version'] & 0xffff:04x} "
          f"(实现 0x{(header['version'] >> 16) & 0xffff:04x})")
    print(f"  Stream 数量      = {header['stream_count']}")
    print(f"  校验和           = 0x{header['checksum']:08x}")
    print(f"  时间戳           = {header['timestamp']}")
    print(f"  标志             = 0x{header['flags']:016x}")
    for st, (sz, rv) in sorted(directory.items()):
        label = STREAM_NAMES.get(st, "0x%08x" % st)
        print(f"  stream {st:<3} {label:20s} size={sz:<8} rva=0x{rv:x}")

    # 4.3 系统信息
    if sysinfo:
        banner("系统信息")
        print(f"  操作系统         = {sysinfo['os']} "
              f"({sysinfo['major']}.{sysinfo['minor']}.{sysinfo['build']})")
        print(f"  CPU 架构         = {sysinfo['arch']} x {sysinfo['nproc']}")
        if sysinfo.get("csd"):
            print(f"  补丁/构建版本    = {sysinfo['csd']}")

    # 4.4 加载模块列表
    if mods:
        dump_modules(mods)

    # 4.5 异常 / 崩溃原因
    if exc:
        banner("异常 / 崩溃原因")
        print(f"  异常代码         = 0x{exc['code']:08x}")
        print(f"  原因描述         = {Colors.RED}{exc['reason']}{Colors.RESET}")
        print(f"  故障地址         = 0x{exc['address']:016x}")
        print(f"  崩溃线程 ID      = {exc['thread_id']}")
        if exc.get("params"):
            print(f"  异常参数:")
            for i, p in enumerate(exc["params"]):
                print(f"    [{i}] 0x{p:016x}")

    # 4.6 崩溃线程寄存器上下文
    banner(f"崩溃线程上下文 (tid={crash_tid})")
    if crashed_reg:
        dump_registers(crashed_reg)
    else:
        print(f"  {Colors.YELLOW}(未捕获寄存器上下文){Colors.RESET}")

    # 4.7 堆栈内存 Hex Dump
    if crashed_reg:
        t = next((x for x in threads if x.thread_id == crash_tid), None)
        if t:
            dump_stack_memory(buf, directory, t, crashed_reg)

    # 4.8 符号化回溯
    rc = show_backtrace(crashed_reg, crash_tid, threads, mods,
                        args.search_dir or [], buf, directory, args)

    # 4.9 其他线程概览
    dump_all_threads(threads, crash_tid, buf, directory, crashed_arch, mods)
    return rc


if __name__ == "__main__":
    sys.exit(main())
