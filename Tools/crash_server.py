"""
崩溃分析 HTTP 服务器
====================
客户端崩溃后上传 .dmp 文件，服务器自动分析并返回结果。

用法:
  python crash_server.py                          # 默认 0.0.0.0:8080
  python crash_server.py --port 9000              # 指定端口
  python crash_server.py --stackwalk /path/to/minidump_stackwalk  # 指定符号化工具

客户端上传示例 (PowerShell):
  Invoke-WebRequest -Uri http://server:8080/analyze -Method POST -InFile crash.dmp -ContentType "application/octet-stream"

客户端上传示例 (curl):
  curl -X POST http://server:8080/analyze --data-binary @crash.dmp

客户端上传示例 (Python):
  import requests
  with open("crash.dmp", "rb") as f:
      r = requests.post("http://server:8080/analyze", data=f)
      print(r.json())
"""

import argparse
import io
import json
import os
import sys
import tempfile
import time
import uuid
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# 将 Tools 目录加入搜索路径，以便导入 crash_analyze
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import crash_analyze


class CrashAnalysisHandler(BaseHTTPRequestHandler):
    """处理崩溃分析请求的 HTTP Handler"""

    # 全局配置（由服务器启动时设置）
    search_dirs = []
    stackwalk_path = None
    symbols_dir = None

    def do_POST(self):
        parsed = urlparse(self.path)

        if parsed.path == "/analyze":
            self._handle_analyze()
        else:
            self._send_json(404, {"error": "Not found. Use POST /analyze"})

    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/" or parsed.path == "/health":
            self._send_json(200, {
                "status": "ok",
                "service": "Crash Analyzer",
                "usage": "POST /analyze with .dmp file body"
            })
        elif parsed.path == "/api/help":
            self._send_json(200, {
                "endpoints": {
                    "POST /analyze": "上传 .dmp 文件进行分析，返回 JSON 结果",
                    "GET /health": "健康检查"
                },
                "client_examples": {
                    "curl": "curl -X POST http://host:port/analyze --data-binary @crash.dmp",
                    "powershell": "Invoke-WebRequest -Uri http://host:port/analyze -Method POST -InFile crash.dmp -ContentType 'application/octet-stream'",
                    "python": "requests.post('http://host:port/analyze', data=open('crash.dmp','rb').read())"
                }
            })
        else:
            self._send_json(404, {"error": "Not found"})

    def _handle_analyze(self):
        """处理崩溃分析请求"""
        content_length = int(self.headers.get("Content-Length", 0))
        if content_length == 0:
            self._send_json(400, {"error": "Empty body. Send .dmp file as request body."})
            return

        if content_length > 100 * 1024 * 1024:  # 100MB 限制
            self._send_json(413, {"error": "File too large (max 100MB)"})
            return

        # 读取上传的 dmp 数据
        body = self.rfile.read(content_length)

        # 写入临时文件
        tmp_dir = tempfile.mkdtemp(prefix="crash_")
        dmp_path = os.path.join(tmp_dir, f"{uuid.uuid4().hex}.dmp")
        try:
            with open(dmp_path, "wb") as f:
                f.write(body)

            # 执行分析
            start = time.time()
            result = self._analyze_dump(dmp_path)
            elapsed = time.time() - start

            result["analysis_time_ms"] = round(elapsed * 1000, 1)
            result["file_size_bytes"] = content_length

            self._send_json(200, result)

        except Exception as e:
            self._send_json(500, {"error": f"Analysis failed: {str(e)}"})
        finally:
            # 清理临时文件
            try:
                os.remove(dmp_path)
                os.rmdir(tmp_dir)
            except OSError:
                pass

    def _analyze_dump(self, dmp_path: str) -> dict:
        """分析 dmp 文件，返回结构化结果"""
        try:
            buf, header, directory = crash_analyze.parse_minidump(dmp_path)
        except Exception as e:
            return {"error": f"Failed to parse minidump: {str(e)}"}

        result = {
            "header": {
                "version": header.get("version", 0),
                "stream_count": header.get("stream_count", 0),
                "timestamp": header.get("timestamp", 0),
            },
            "modules": [],
            "exception": None,
            "threads": [],
            "crash_summary": None,
            "backtrace": None,
        }

        # 系统信息
        sysinfo = None
        if crash_analyze.MD_SYSTEM_INFO_STREAM in directory:
            sysinfo = crash_analyze.read_system_info(buf, directory[crash_analyze.MD_SYSTEM_INFO_STREAM])
            result["system_info"] = sysinfo

        # 模块列表
        mods = []
        if crash_analyze.MD_MODULE_LIST_STREAM in directory:
            mods = crash_analyze.read_module_list(buf, directory[crash_analyze.MD_MODULE_LIST_STREAM])
            result["modules"] = [
                {
                    "name": m.name,
                    "base": f"0x{m.base:016x}",
                    "base_int": m.size,
                    "size": m.size,
                    "path": m.path,
                }
                for m in mods
            ]

        # 异常信息
        exc = None
        if crash_analyze.MD_EXCEPTION_STREAM in directory:
            exc = crash_analyze.read_exception(buf, directory[crash_analyze.MD_EXCEPTION_STREAM])
            result["exception"] = {
                "code": f"0x{exc['code']:08x}",
                "code_int": exc["code"],
                "reason": exc.get("reason", ""),
                "address": f"0x{exc['address']:016x}",
                "address_int": exc["address"],
                "thread_id": exc["thread_id"],
                "parameters": [f"0x{p:016x}" for p in exc.get("params", [])],
            }

        # 线程列表
        threads = []
        if crash_analyze.MD_THREAD_LIST_STREAM in directory:
            threads = crash_analyze.read_thread_list(buf, directory[crash_analyze.MD_THREAD_LIST_STREAM])
            result["thread_count"] = len(threads)

        # 崩溃线程
        crash_tid = exc["thread_id"] if exc else (
            threads[0].thread_id if threads else -1)

        # 寄存器上下文
        crashed_reg = None
        crashed_arch = sysinfo["arch"] if sysinfo else "unknown"
        if exc and exc.get("ctx_rva"):
            data = buf[exc["ctx_rva"]:exc["ctx_rva"] + exc["ctx_size"]]
            crashed_reg = crash_analyze.parse_context(data, crashed_arch)
        elif threads:
            t = next((x for x in threads if x.thread_id == crash_tid), None)
            if t and t.context_rva and t.context_size:
                data = buf[t.context_rva:t.context_rva + t.context_size]
                crashed_reg = crash_analyze.parse_context(data, crashed_arch)

        # 匹配崩溃 PC 到模块
        if exc and crashed_reg:
            pc = exc["address"]
            mod = crash_analyze.find_module_for_pc(mods, pc)
            offset = pc - mod.base if mod else None
            result["crash_summary"] = {
                "exception_code": f"0x{exc['code']:08x}",
                "exception_reason": exc.get("reason", ""),
                "crash_address": f"0x{pc:016x}",
                "crash_module": mod.name if mod else None,
                "crash_offset": f"0x{offset:x}" if offset is not None else None,
                "thread_id": crash_tid,
            }

            # 寄存器
            result["registers"] = {
                "pc": f"0x{crashed_reg.pc:016x}" if crashed_reg.pc else None,
                "sp": f"0x{crashed_reg.sp:016x}" if crashed_reg.sp else None,
                "fp": f"0x{crashed_reg.fp:016x}" if crashed_reg.fp else None,
                "lr": f"0x{crashed_reg.lr:016x}" if crashed_reg.lr else None,
            }

            # 简单回溯（帧指针 walk）
            frames = crash_analyze.frame_pointer_walk(
                crashed_reg, threads, mods, buf, directory, crashed_arch)
            if frames:
                bt = []
                for addr in frames:
                    m = crash_analyze.find_module_for_pc(mods, addr)
                    if m:
                        off = addr - m.base
                        bt.append({"address": f"0x{addr:016x}", "module": m.name, "offset": f"0x{off:x}"})
                    else:
                        bt.append({"address": f"0x{addr:016x}", "module": None, "offset": None})
                result["backtrace"] = bt

        return result

    def _send_json(self, status: int, data: dict):
        """发送 JSON 响应"""
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        """自定义日志格式"""
        sys.stderr.write(f"[{time.strftime('%H:%M:%S')}] {args[0]}\n")


def main():
    ap = argparse.ArgumentParser(description="崩溃分析 HTTP 服务器")
    ap.add_argument("--host", default="0.0.0.0", help="监听地址 (默认 0.0.0.0)")
    ap.add_argument("--port", type=int, default=8080, help="监听端口 (默认 8080)")
    ap.add_argument("--search-dir", action="append", default=[],
                    help="构建输出目录 (可多次指定)")
    ap.add_argument("--stackwalk", default=None,
                    help="minidump_stackwalk 路径")
    ap.add_argument("--symbols-dir", default=None,
                    help="Breakpad .sym 符号文件目录")
    args = ap.parse_args()

    # 设置 Handler 的全局配置
    CrashAnalysisHandler.search_dirs = args.search_dir
    CrashAnalysisHandler.stackwalk_path = args.stackwalk
    CrashAnalysisHandler.symbols_dir = args.symbols_dir

    server = HTTPServer((args.host, args.port), CrashAnalysisHandler)
    print(f"崩溃分析服务器已启动: http://{args.host}:{args.port}")
    print(f"  POST /analyze  - 上传 .dmp 文件进行分析")
    print(f"  GET  /health   - 健康检查")
    print(f"  Ctrl+C 停止")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止")
        server.server_close()


if __name__ == "__main__":
    main()
