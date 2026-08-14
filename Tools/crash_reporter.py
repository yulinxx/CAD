"""
崩溃上报客户端
=============
CAD 程序崩溃后自动将 .dmp 文件发送到分析服务器。

集成方式:
  1. 在程序启动时初始化: client = CrashReporter("http://your-server:8080")
  2. 在崩溃回调中调用: client.report("path/to/crash.dmp")

或者独立使用:
  python crash_reporter.py http://server:8080 crash.dmp
"""

import json
import sys
import urllib.request
import urllib.error


class CrashReporter:
    """崩溃上报客户端"""

    def __init__(self, server_url: str, timeout: int = 30):
        """
        Args:
            server_url: 分析服务器地址, 如 "http://192.168.1.100:8080"
            timeout:    上传超时时间(秒)
        """
        self.server_url = server_url.rstrip("/")
        self.timeout = timeout

    def report(self, dmp_path: str) -> dict:
        """上传 .dmp 文件到服务器并返回分析结果
        
        Args:
            dmp_path: .dmp 文件路径
            
        Returns:
            服务器返回的分析结果 (dict)
        """
        with open(dmp_path, "rb") as f:
            data = f.read()

        url = f"{self.server_url}/analyze"
        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Content-Type", "application/octet-stream")

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                result = json.loads(resp.read().decode("utf-8"))
                return result
        except urllib.error.URLError as e:
            return {"error": f"连接服务器失败: {e}"}
        except Exception as e:
            return {"error": f"上报失败: {e}"}

    def report_and_print(self, dmp_path: str):
        """上传并打印分析结果"""
        print(f"正在上传 {dmp_path} ...")
        result = self.report(dmp_path)

        if "error" in result:
            print(f"[错误] {result['error']}")
            return

        summary = result.get("crash_summary", {})
        print(f"\n{'='*60}")
        print(f"  崩溃分析结果")
        print(f"{'='*60}")
        print(f"  异常代码: {summary.get('exception_code', 'N/A')}")
        print(f"  异常原因: {summary.get('exception_reason', 'N/A')}")
        print(f"  崩溃地址: {summary.get('crash_address', 'N/A')}")
        print(f"  崩溃模块: {summary.get('crash_module', 'N/A')}")
        print(f"  模块偏移: {summary.get('crash_offset', 'N/A')}")
        print(f"  崩溃线程: {summary.get('thread_id', 'N/A')}")
        print(f"  分析耗时: {result.get('analysis_time_ms', 'N/A')}ms")

        bt = result.get("backtrace", [])
        if bt:
            print(f"\n  调用栈:")
            for i, frame in enumerate(bt):
                mod = frame.get("module", "?")
                off = frame.get("offset", "?")
                addr = frame.get("address", "?")
                print(f"    [{i:2d}] {addr} {mod}+{off}")

        print(f"{'='*60}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"用法: python {sys.argv[0]} <server_url> <crash.dmp>")
        print(f"示例: python {sys.argv[0]} http://192.168.1.100:8080 crash.dmp")
        sys.exit(1)

    server = sys.argv[1]
    dmp = sys.argv[2]
    reporter = CrashReporter(server)
    reporter.report_and_print(dmp)
