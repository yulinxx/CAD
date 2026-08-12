#!/usr/bin/env python3
"""
crash_analyze.py - Cross-platform Breakpad minidump crash analyzer.

Parses a Breakpad *.dmp minidump (the format written by the project's
CrashHandler module on Windows / Linux / macOS) and prints:

  * minidump header (version, flags, stream count)
  * system info (OS, CPU architecture, build, processors)
  * crash reason / exception code & faulting address
  * the crashing thread's full register set
  * the module list (base address, size, on-disk path) - needed to
    resolve symbolication
  * a symbolicated backtrace:
      - uses an external minidump_stackwalk when available (best)
      - otherwise falls back to a built-in frame-pointer walker that
        resolves the crashing PC, the LR (caller) frame and deeper frames
        using the platform native symbolizer (atos / addr2line /
        llvm-symbolizer) against the binaries found via --search-dir.

Usage:
  python crash_analyze.py /path/to/crash.dmp
  python crash_analyze.py /path/to/crash.dmp --search-dir build/bin_Qt6/Debug
  python crash_analyze.py /path/to/crash.dmp --dump-stackwalk /opt/breakpad/minidump_stackwalk
  python crash_analyze.py /path/to/crash.dmp --raw        # no symbolication

Exit status:
  0  dumped successfully
  1  could not parse the file (not a minidump)
  2  parsed but the crashing PC could not be mapped to a module
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Constants (from google_breakpad minidump_format.h)
# ---------------------------------------------------------------------------

MD_HEADER_SIGNATURE = 0x504D444D  # 'PMDM' little-endian

MD_THREAD_LIST_STREAM = 3
MD_MODULE_LIST_STREAM = 4
MD_MEMORY_LIST_STREAM = 5
MD_EXCEPTION_STREAM = 6
MD_SYSTEM_INFO_STREAM = 7

MD_CONTEXT_AMD64 = 0x00100000
MD_CONTEXT_ARM64 = 0x00400000

# Stream-type -> human readable label
STREAM_NAMES = {
    3: "ThreadList", 4: "ModuleList", 5: "MemoryList", 6: "Exception",
    7: "SystemInfo",
}

# ---------------------------------------------------------------------------
# Small data containers
# ---------------------------------------------------------------------------


@dataclass
class ModuleEntry:
    name: str        # basename as recorded in the dump
    base: int        # BaseOfImage
    size: int        # SizeOfImage
    path: str        # full path if available, else name
    timestamp: int
    checksum: int


@dataclass
class ThreadEntry:
    thread_id: int
    stack_base: int
    context_rva: int
    context_size: int


@dataclass
class RegisterSet:
    pc: int
    sp: int
    fp: Optional[int]
    lr: int
    arch: str
    extra: Dict[str, int] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Minidump structure parsing
# ---------------------------------------------------------------------------


def read_mdstring(buf: bytes, rva: int) -> str:
    """Read an MDString at the given RVA: u32 length + UTF-16LE chars."""
    if rva == 0:
        return ""
    length = struct.unpack_from("<I", buf, rva)[0]
    raw = buf[rva + 4:rva + 4 + length]
    try:
        return raw.decode("utf-16-le", errors="replace")
    except Exception:
        return raw.decode("latin-1", errors="replace")


def parse_minidump(path: str):
    with open(path, "rb") as f:
        buf = f.read()
    if len(buf) < 32:
        raise ValueError("file too small to be a minidump")
    sig, version, stream_count, dir_rva, checksum, timestamp, flags = \
        struct.unpack_from("<IIIIIIQ", buf, 0)
    if sig != MD_HEADER_SIGNATURE:
        raise ValueError("not a minidump (bad signature 0x%08x)" % sig)

    header = {
        "version": version,
        "stream_count": stream_count,
        "checksum": checksum,
        "timestamp": timestamp,
        "flags": flags,
    }

    directory: Dict[int, Tuple[int, int]] = {}
    for i in range(stream_count):
        b = dir_rva + i * 12  # MDRawDirectory = stream_type(4)+MDLocationDescriptor(8)
        stream_type, data_size, rva = struct.unpack_from("<III", buf, b)
        directory[stream_type] = (data_size, rva)

    return buf, header, directory


def read_system_info(buf: bytes, loc: Tuple[int, int]) -> dict:
    size, rva = loc
    arch = struct.unpack_from("<H", buf, rva + 0)[0]
    _level = struct.unpack_from("<H", buf, rva + 2)[0]
    _rev = struct.unpack_from("<H", buf, rva + 4)[0]
    nproc = buf[rva + 6]
    ptype = buf[rva + 7]
    major = struct.unpack_from("<I", buf, rva + 8)[0]
    minor = struct.unpack_from("<I", buf, rva + 12)[0]
    build = struct.unpack_from("<I", buf, rva + 16)[0]
    platform = struct.unpack_from("<I", buf, rva + 20)[0]
    csd_rva = struct.unpack_from("<I", buf, rva + 24)[0]
    arch_names = {
        0: "x86", 5: "arm", 9: "amd64", 12: "arm64",
        0x8001: "sparc", 0x8002: "ppc64",
        0x8003: "arm64-old", 0x8004: "mips64",
        0x8005: "riscv", 0x8006: "riscv64",
    }
    os_names = {0: "Win32s", 1: "Win32Windows", 2: "Win32NT",
                0x8000: "Unix", 0x8101: "MacOS", 0x8102: "iOS",
                0x8201: "Linux", 0x8203: "Android"}
    return {
        "arch": arch_names.get(arch, "0x%x" % arch),
        "os": os_names.get(platform, "0x%x" % platform),
        "major": major, "minor": minor, "build": build,
        "nproc": nproc, "csd": read_mdstring(buf, csd_rva),
    }


def read_thread_list(buf: bytes, loc: Tuple[int, int]) -> List[ThreadEntry]:
    size, rva = loc
    count = struct.unpack_from("<I", buf, rva)[0]
    threads = []
    # MDRawThread = 48 bytes. The list header is number_of_threads (uint32)
    # followed by 4 bytes of alignment padding on 64-bit dumps, so the
    # first MDRawThread sits at (rva + 8), not (rva + 4).
    rec = 48
    header = 8
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
    """Extract the PDB/ELF image name from an MDRawModule.cv_record.

    Modern Crashpad/Crashpad-on-macOS minidumps store the module basename in
    the CodeView record (RSDS/PDB70: 'RSDS' sig + GUID + age + 0-terminated
    ASCII name), NOT in module_name_rva as a MINIDUMP_STRING. This helper
    reads both and returns the first non-empty name.
    """
    if not cv_rva or not cv_size:
        return ""
    data = buf[cv_rva:cv_rva + cv_size]
    if len(data) < 4:
        return ""
    sig = data[:4]
    if sig in (b"RSDS", b"SDSR", b"NB07"):  # PDB70/20
        # 4 (sig) + 16 (GUID) + 4 (age) = 24, then ASCII name
        name = data[24:].split(b"\x00", 1)[0]
        try:
            return name.decode("utf-8", errors="replace")
        except Exception:
            return ""
    return ""


def _is_plausible_base(v: int) -> bool:
    # macOS image bases live in 0x100000000..0x7fffffffffff; Windows similar.
    return 0x100000000 <= v <= 0x7FFFFFFFFFFF and (v & 0xFFF) == 0


def read_module_list(buf: bytes, loc: Tuple[int, int]) -> List[ModuleEntry]:
    size, rva = loc
    count = struct.unpack_from("<I", buf, rva)[0]
    region = buf[rva:rva + size]

    # The stride between MDRawModule records: MD_MODULE_SIZE = 108 bytes
    # (Breakpad classic Linux & Crashpad macOS both use 108 here). The list
    # header is number_of_modules (uint32) + 4 bytes alignment padding on
    # 64-bit dumps, so the first record sits at (rva + 8).
    best_stride, best_score = 108, -1
    for stride in (108, 112, 116, 120, 128, 160, 200, 216, 224):
        score = 0
        checked = 0
        for i in range(count):
            b = rva + 8 + i * stride
            if b + 16 > len(buf):
                break
            checked += 1
            base = struct.unpack_from("<Q", buf, b)[0]
            if base == 0 or _is_plausible_base(base):
                score += 1
            else:
                break
        # prefer the stride that validates the most *trailing* records too
        if checked == count and score >= best_score and score > 0:
            best_stride, best_score = stride, score

    mods: List[ModuleEntry] = []
    hdr = 8  # number_of_modules (uint32) + 4-byte alignment pad
    for i in range(count):
        b = rva + hdr + i * best_stride
        if b + 108 > len(buf):
            break
        base = struct.unpack_from("<Q", buf, b + 0)[0]
        img_size = struct.unpack_from("<I", buf, b + 8)[0]
        checksum = struct.unpack_from("<I", buf, b + 12)[0]
        tds = struct.unpack_from("<I", buf, b + 16)[0]
        # cv_record MDLocationDescriptor is at offset 76 in MDRawModule
        cv_size = struct.unpack_from("<I", buf, b + 76)[0]
        cv_rva = struct.unpack_from("<I", buf, b + 80)[0]
        name = cv_record_name(buf, cv_rva, cv_size)
        if not name and _is_plausible_base(base):
            name = hex(base)
        # only keep real loaded images (skip zero-base placeholders)
        if base == 0 or not _is_plausible_base(base):
            continue
        mods.append(ModuleEntry(name, base, img_size, name, tds, checksum))
    return mods


def read_exception(buf: bytes, loc: Tuple[int, int]) -> dict:
    size, rva = loc
    tid = struct.unpack_from("<I", buf, rva + 0)[0]
    code = struct.unpack_from("<I", buf, rva + 8)[0]
    flags = struct.unpack_from("<I", buf, rva + 12)[0]
    addr = struct.unpack_from("<Q", buf, rva + 24)[0]
    nparam = struct.unpack_from("<I", buf, rva + 32)[0]
    params = list(struct.unpack_from("<%dQ" % nparam, buf, rva + 40))[0:nparam]
    # The embedded thread-context is a MINIDUMP_LOCATION_DESCRIPTOR which is
    # stored as { Size (u32), Rva (u32) } - i.e. size comes first. Earlier
    # code read these two u32s in the wrong order, which made it slice a
    # garbage window out of the file instead of the real context record.
    ctx_size = struct.unpack_from("<I", buf, rva + 160)[0]
    ctx_rva = struct.unpack_from("<I", buf, rva + 164)[0]
    reason = describe_exception(code)
    return {"code": code, "reason": reason, "address": addr,
            "thread_id": tid, "ctx_rva": ctx_rva, "ctx_size": ctx_size,
            "params": params, "flags": flags}


def describe_exception(code: int) -> str:
    mac = {1: "EXC_BAD_ACCESS (invalid address)",
           2: "EXC_BAD_ACCESS (protection failure)",
           0x00010000: "SIGSYS/BAD_SYSCALL",
           0x00010001: "SIGPIPE"}
    if code in mac:
        return mac[code]
    win = {0xC0000005: "ACCESS_VIOLATION", 0xC00000FD: "STACK_OVERFLOW"}
    if code in win:
        return win[code]
    if code == 0x00000000:
        return "EXC_BAD_ACCESS"
    return "0x%08x" % code


# ---------------------------------------------------------------------------
# Context (register) record parsing
# ---------------------------------------------------------------------------


def parse_amd64_context(data: bytes) -> RegisterSet:
    # MDRawContextAMD64: after p1home(48)+ctxflags+mxcsr(8)=56 and the 6 debug
    # registers (48) = 104, the GPR block (rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,
    # r8..r15) starts. rsp=off+4*8, rbp=off+5*8, rip=off+16*8.
    gpr = 104
    rsp = struct.unpack_from("<Q", data, gpr + 4 * 8)[0]
    rbp = struct.unpack_from("<Q", data, gpr + 5 * 8)[0]
    rip = struct.unpack_from("<Q", data, gpr + 16 * 8)[0]
    names = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
    extra = {n: struct.unpack_from("<Q", data, gpr + i * 8)[0]
             for i, n in enumerate(names) if n != "rip"}
    return RegisterSet(rip, rsp, rbp if rbp else None, rip, "amd64", extra)


def parse_arm64_context(data: bytes) -> RegisterSet:
    # MDRawContextARM64: context_flags(4)+cpsr(4)+ iregs[33]
    off = 8
    x = [struct.unpack_from("<Q", data, off + i * 8)[0] for i in range(33)]
    extra = {f"x{i}": x[i] for i in range(32)}
    return RegisterSet(x[32], x[33 - 2], x[29] if x[29] else None,
                       x[30], "arm64", extra)


def parse_x86_context(data: bytes) -> RegisterSet:
    # MDRawContextX86: best-effort. Integer GPR block begins after dr(32)+seg(12)
    # = offset 44. ebp/eip/esp offsets are fragile across toolchains.
    gpr = 44
    try:
        esp = struct.unpack_from("<I", data, gpr + 19 * 4)[0]
        ebp = struct.unpack_from("<I", data, gpr + 18 * 4)[0]
        eip = struct.unpack_from("<I", data, gpr + 20 * 4)[0]
    except Exception:
        eip = esp = ebp = 0
    return RegisterSet(eip, esp, ebp if ebp else None, eip, "x86")


def parse_context(data: bytes, arch: str) -> Optional[RegisterSet]:
    """Dispatch a raw MINIDUMP_CONTEXT blob to the arch-specific parser.

    The arch comes from the SYSTEM_INFO stream, because macOS Crashpad writes
    arm64 context flags in a non-standard encoding (0x80000006) that does not
    match Breakpad's MD_CONTEXT_ARM64 (0x00400000), so dispatching on the
    context_flags word is unreliable across platforms.
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


# ---------------------------------------------------------------------------
# Symbolication
# ---------------------------------------------------------------------------


def find_module_for_pc(mods: List[ModuleEntry], pc: int) -> Optional[ModuleEntry]:
    for m in mods:
        if m.base <= pc < m.base + max(m.size, 1):
            return m
    return None


def best_symbolizer() -> Optional[str]:
    for t in ["atos", "addr2line", "llvm-symbolizer"]:
        if shutil.which(t):
            return t
    return None


def candidate_binaries(mod: ModuleEntry, search_dirs: List[str]) -> List[str]:
    out: List[str] = []
    if mod.path and os.path.isfile(mod.path):
        out.append(mod.path)
    name = mod.path or mod.name
    base = os.path.basename(name)
    # On macOS the dump often records a framework/dylib basename like
    # "libSanYiRender_d.1.0.0.dylib"; match by prefix to catch versioned copies.
    for d in search_dirs:
        if not d or not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for fn in files:
                if fn == base or fn.startswith(base.split(".")[0]):
                    cand = os.path.join(root, fn)
                    if cand not in out:
                        out.append(cand)
    return out


def symbolicate(pc: int, mod: ModuleEntry, binary: str,
                loader: str) -> Optional[str]:
    if loader == "atos":
        cmd = ["atos", "-o", binary, "-l", hex(mod.base), hex(pc)]
    elif loader == "llvm-symbolizer":
        # llvm-symbolizer expects an address relative to the image load
        cmd = ["llvm-symbolizer", "--obj=" + binary,
               "--addresses=%d" % (pc - mod.base), "--fbase=%d" % mod.base]
    else:  # addr2line
        cmd = [loader, "-e", binary, "@%.16lx" % (pc - mod.base)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    except Exception as e:
        return None
    if out.returncode != 0 or not out.stdout.strip():
        return None
    # Prefer the line containing a file:lineno pattern if multiple lines
    lines = [ln.strip() for ln in out.stdout.splitlines() if ln.strip()]
    if not lines:
        return None
    return "; ".join(lines) if len(lines) == 1 else lines[0]


def read_thread_stack_memory(buf: bytes, directory: dict, t: ThreadEntry):
    """Best-effort read of a thread's stack bytes from MEMORY_LIST_STREAM (5).
    MDMemoryDescriptor = uint64 start + MDLocationDescriptor(4+4) = 16 bytes."""
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
                       max_frames: int = 8) -> List[str]:
    """Walk caller frames via the frame-pointer chain using captured stack
    memory. Returns a list of 'frame N: <symbol>' lines (excluding frame 0)."""
    t = next((x for x in threads if x.thread_id == crash_tid), None)
    if not t or not reg.fp or args_check_no_raw():
        return []
    stack, base = read_thread_stack_memory(buf, directory, t)
    if not stack:
        return []
    loader = best_symbolizer()
    frames: List[str] = []
    fp = reg.fp
    ptr_size = 8
    seen = set()

    def read_ptr(addr: int) -> int:
        off = addr - base
        if 0 <= off < len(stack) - ptr_size + 1:
            return struct.unpack_from("<Q", stack, off)[0]
        return 0

    for depth in range(max_frames):
        if fp in seen or not (base <= fp < base + len(stack) - 2 * ptr_size + 1):
            break
        seen.add(fp)
        next_fp = read_ptr(fp)
        ret_addr = read_ptr(fp + ptr_size)
        if ret_addr == 0:
            break
        mod = find_module_for_pc(mods, ret_addr)
        sym = (symbolicate(ret_addr, mod, c, loader)
               for c in candidate_binaries(mod, search_dirs) if mod) if mod else []
        # take first non-None from a generator
        resolved = next((s for s in sym if s), None)
        frames.append(resolved or f"0x{ret_addr:x}")
        if next_fp == 0:
            break
        fp = next_fp
    return frames


# Module-level flag so frame_pointer_walk can honour --raw without passing args
_NO_RAW_WALK = False


def args_check_no_raw() -> bool:
    return _NO_RAW_WALK


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def banner(title: str) -> None:
    print("\n" + "=" * 64)
    print(title)
    print("=" * 64)


def dump_registers(rs: RegisterSet) -> None:
    print(f"  PC   = 0x{rs.pc:016x}")
    print(f"  SP   = 0x{rs.sp:016x}")
    if rs.fp is not None:
        print(f"  FP   = 0x{rs.fp:016x}")
    print(f"  LR   = 0x{rs.lr:016x}")
    for name in sorted(rs.extra):
        print(f"  {name:5s}= 0x{rs.extra[name]:016x}")


def show_backtrace(crashed_reg: Optional[RegisterSet], crash_tid: int,
                   threads: List[ThreadEntry], mods: List[ModuleEntry],
                   search_dirs: List[str], buf: bytes, directory: dict,
                   args) -> int:
    """Print a symbolicated backtrace and return 0 on success or 2 if the
    crashing PC could not be matched to any module."""
    banner("BACKTRACE")

    sw = args.dump_stackwalk or shutil.which("minidump_stackwalk")
    if sw:
        print(f"[i] delegating full stack walk to: {sw}")
        cmd = [sw, args.dump]
        if args.symbols_dir:
            cmd += [args.symbols_dir]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if out.returncode == 0:
                for line in out.stdout.splitlines():
                    print("  " + line)
                return 0
            print(out.stdout)
            print(out.stderr, file=sys.stderr)
        except Exception as e:
            print(f"[!] minidump_stackwalk failed: {e}", file=sys.stderr)
        print("[i] falling back to built-in parser.\n")

    if not crashed_reg:
        print("  (no thread context captured - cannot backtrace)")
        return 0

    loader = best_symbolizer() or "(no symbolizer found)"
    print(f"[i] symbolizer: {loader}")

    # Frame 0 - crashing PC.
    pc = crashed_reg.pc
    pc_note = ""
    mod0 = find_module_for_pc(mods, pc)
    if (not mod0 or pc == 0) and crashed_reg.lr:
        # On arm64, an indirect call through a null function pointer records
        # pc=0 (and faulting address 0). The real faulting site is the call
        # instruction itself, whose address is the recorded LR.
        mod_lr = find_module_for_pc(mods, crashed_reg.lr)
        if (not mod0 or pc == 0) and mod_lr:
            pc, mod0, pc_note = crashed_reg.lr, mod_lr, (
                " (pc was 0x0 - null indirect call; pc recovered from LR)")
    if not mod0:
        print(f"  frame  0: pc=0x{crashed_reg.pc:x} (no matching module)")
        return 2
    line0 = None
    for c in candidate_binaries(mod0, search_dirs):
        line0 = symbolicate(pc, mod0, c, loader)
        if line0:
            break
    fallback0 = "pc=0x%x (image=%s base=0x%x)" % (pc, mod0.name, mod0.base)
    print(f"  frame  0: {line0 or fallback0}{pc_note}")

    # Frame 1 - return address (LR). Skip when frame 0 was already recovered
    # from LR, to avoid printing the same address twice.
    if crashed_reg.lr and crashed_reg.lr != crashed_reg.pc and not pc_note:
        mod1 = find_module_for_pc(mods, crashed_reg.lr)
        line1 = None
        if mod1:
            for c in candidate_binaries(mod1, search_dirs):
                line1 = symbolicate(crashed_reg.lr, mod1, c, loader)
                if line1:
                    break
        print(f"  frame  1: {line1 or 'lr=0x%x' % crashed_reg.lr}")

    # Deeper frames via the frame-pointer chain (only if not --raw).
    deeper = frame_pointer_walk(crashed_reg, crash_tid, threads, mods,
                                search_dirs, buf, directory)
    for i, line in enumerate(deeper, start=2):
        print(f"  frame {i:2d}: {line}")

    if not deeper and not crashed_reg.lr:
        print("  (frame-pointer chain not available in this dump).")
    print("  [note] for a full trace, build & use Breakpad "
          "`minidump_stackwalk` (pass --dump-stackwalk).")
    return 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Cross-platform Breakpad minidump crash analyzer.")
    ap.add_argument("dump", help="path to *.dmp")
    ap.add_argument("--search-dir", action="append", default=[],
                    help="build/bin directories to locate debug binaries "
                         "(repeatable)")
    ap.add_argument("--symbols-dir", default=None,
                    help="directory of Breakpad .sym files (for minidump_stackwalk)")
    ap.add_argument("--raw", action="store_true",
                    help="only print parsed crash info, skip symbolication")
    ap.add_argument("--dump-stackwalk", default=None,
                    help="path to minidump_stackwalk binary (Breakpad tool)")
    args = ap.parse_args()

    global _NO_RAW_WALK
    _NO_RAW_WALK = args.raw

    try:
        buf, header, directory = parse_minidump(args.dump)
    except Exception as e:
        print(f"[!] {e}", file=sys.stderr)
        return 1

    banner("MINIDUMP HEADER")
    print(f"  version          = 0x{header['version'] & 0xffff:04x} "
          f"(impl 0x{(header['version'] >> 16) & 0xffff:04x})")
    print(f"  stream_count     = {header['stream_count']}")
    print(f"  checksum         = 0x{header['checksum']:08x}")
    print(f"  timestamp        = {header['timestamp']}")
    print(f"  flags            = 0x{header['flags']:016x}")
    for st, (sz, rv) in sorted(directory.items()):
        label = STREAM_NAMES.get(st, "0x%08x" % st)
        print(f"  stream {st:<3} {label:20s} size={sz} rva=0x{rv:x}")

    sysinfo: Optional[dict] = None
    if MD_SYSTEM_INFO_STREAM in directory:
        sysinfo = read_system_info(buf, directory[MD_SYSTEM_INFO_STREAM])
        banner("SYSTEM INFO")
        print(f"  OS               = {sysinfo['os']} "
              f"({sysinfo['major']}.{sysinfo['minor']}.{sysinfo['build']})")
        print(f"  CPU              = {sysinfo['arch']} x {sysinfo['nproc']}")
        if sysinfo.get("csd"):
            print(f"  CSD / build ver  = {sysinfo['csd']}")

    mods: List[ModuleEntry] = []
    if MD_MODULE_LIST_STREAM in directory:
        mods = read_module_list(buf, directory[MD_MODULE_LIST_STREAM])
        banner("MODULES")
        print(f"  {'base':<18} {'size':<10} name")
        for m in mods:
            print(f"  0x{m.base:016x} {m.size:<10} {m.name}")

    exc: Optional[dict] = None
    if MD_EXCEPTION_STREAM in directory:
        exc = read_exception(buf, directory[MD_EXCEPTION_STREAM])
        banner("EXCEPTION / CRASH REASON")
        print(f"  exception code   = 0x{exc['code']:08x}")
        print(f"  reason           = {exc['reason']}")
        print(f"  faulting address = 0x{exc['address']:016x}"
              if exc['address'] else "  faulting address = 0x0")
        print(f"  thread id        = {exc['thread_id']}")

    threads: List[ThreadEntry] = []
    if MD_THREAD_LIST_STREAM in directory:
        threads = read_thread_list(buf, directory[MD_THREAD_LIST_STREAM])

    crash_tid = exc['thread_id'] if exc else (
        threads[0].thread_id if threads else -1)

    crashed_reg: Optional[RegisterSet] = None
    crashed_arch = sysinfo['arch'] if sysinfo else "unknown"
    if exc and exc.get("ctx_rva"):
        size = exc['ctx_size']
        rva = exc['ctx_rva']
        data = buf[rva:rva + size]
        crashed_reg = parse_context(data, crashed_arch)
    elif threads:
        # Fall back to the crashing thread's context in the thread list.
        t = next((x for x in threads if x.thread_id == crash_tid), None)
        if t and t.context_rva and t.context_size:
            data = buf[t.context_rva:t.context_rva + t.context_size]
            crashed_reg = parse_context(data, crashed_arch)

    banner(f"CRASHING THREAD (tid={crash_tid})")
    if crashed_reg:
        dump_registers(crashed_reg)
    else:
        print("  (no context captured)")

    rc = show_backtrace(crashed_reg, crash_tid, threads, mods,
                        args.search_dir or [], buf, directory, args)
    # Print remaining (non-crashing) threads for completeness.
    if len(threads) > 1:
        print("\n  other threads:")
        for t in threads:
            if t.thread_id == crash_tid:
                continue
            print(f"  tid={t.thread_id} stack=0x{t.stack_base:x}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
