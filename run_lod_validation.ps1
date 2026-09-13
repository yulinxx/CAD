# LOD 跨层设计验证脚本：自动运行 LodValidationTest（精度 + 帧率）
# 用法（在项目根目录执行）：
#   .\run_lod_validation.ps1                    # 默认 RelWithDebInfo
#   .\run_lod_validation.ps1 -Config Release    # 指定配置
param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "bin_Qt6\$Config\Engine2DTests.exe"

Write-Host "== LOD 跨层设计验证（配置：$Config）==" -ForegroundColor Cyan

if (!(Test-Path $exe)) {
    Write-Host "未找到 $exe，先编译 Engine2DTests ..." -ForegroundColor Yellow
    cmake --build $buildDir --target Engine2DTests --config $Config
    if ($LASTEXITCODE -ne 0) {
        Write-Host "编译失败" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# 只跑 LOD 验证用例，输出精度与帧率结果
& $exe --gtest_filter="LodValidationTest.*"
exit $LASTEXITCODE
