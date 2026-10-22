#requires -Version 5.1
<#
.SYNOPSIS
    Raster SDK delivery guard (standalone build + consumers + ABI/dependency assertions).

.DESCRIPTION
    Builds the Raster module through its standalone entry, installs the SDK, and
    then asserts the delivery contract that a customer depends on. Any violation
    fails the script with a non-zero exit code, so this can be used as a CI gate.

    Checks:
      1. build      - standalone configure/build (Release, plus Debug for artifact naming)
      2. layout     - include/ + lib/ + bin/ + lib/cmake/ contain exactly the SDK, nothing internal
      3. exports    - every RASTER_API declaration in RasterApi.h is exported, and no
                      foreign symbol (C++ mangled names, Qt) leaks out
      4. deps       - the DLL imports only the MSVC runtime and OpenCV; no Qt/spdlog,
                      and no Debug CRT in the Release binary
      5. consumers  - two projects built *outside* the module and against the installed SDK
                      only: one plain C (manual include/lib) and one C++ (find_package)

    Windows/MSVC only: the export and dependency assertions rely on dumpbin, and the
    Debug import library naming is MSVC specific. ELF/Mach-O equivalents would need
    nm/readelf/otool and are not covered here.

    Note: this script intentionally does not run the unit tests. Those are a separate
    gate (ctest); this one only guards what the *delivered* artifacts look like.

.PARAMETER BuildDir
    Scratch directory for the verify build. Default: <repo>/build/raster-sdk-verify

.PARAMETER VcpkgDir
    vcpkg root passed as -DVCPKG_DIR. Defaults to $env:VCPKG_ROOT when set, otherwise
    the repository default from Config.cmake is used.

.PARAMETER Architecture
    Platform passed to Visual Studio generators. Default: x64

.PARAMETER SkipDebug
    Skip the Debug configuration (artifact naming and export parity checks).

.PARAMETER Clean
    Remove the scratch directory before starting.

.PARAMETER Package
    Also produce the deliverable archive (a .zip with MANIFEST.txt and
    VERSION.txt) from the verified SDK. The archive is written next to the
    scratch directory unless -PackagePath says otherwise.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Raster/Tools/VerifySdk.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File Raster/Tools/VerifySdk.ps1 -Package
#>

[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$VcpkgDir = "",
    [string]$Architecture = "x64",
    [switch]$SkipDebug,
    [switch]$Clean,
    [switch]$Package,
    [string]$PackagePath = ""
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Locate the repository root and the module.
# ---------------------------------------------------------------------------
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$moduleRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot "build/raster-sdk-verify"
}
# Consumers are configured from a different source directory, so every path handed to
# CMake must be absolute or it is re-interpreted relative to the consumer build dir.
foreach ($pathName in @("BuildDir", "VcpkgDir")) {
    $value = Get-Variable -Name $pathName -ValueOnly
    if ($value -and -not [System.IO.Path]::IsPathRooted($value)) {
        Set-Variable -Name $pathName -Value ([System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $value)))
    }
}
if (-not $VcpkgDir -and $env:VCPKG_ROOT) {
    # 只有当 VCPKG_ROOT 真的是一个可用的 vcpkg（带 toolchain 且已装过包）才用它。
    # Visual Studio 自带的那份 vcpkg（<VS>/VC/vcpkg）没有 toolchain 也没有
    # installed/，直接用会让 configure 死在 _find_package 上。
    $candidate = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    if ((Test-Path $candidate) -and (Test-Path (Join-Path $env:VCPKG_ROOT "installed"))) {
        $VcpkgDir = $env:VCPKG_ROOT
    }
    else {
        Write-Host "  note: VCPKG_ROOT='$env:VCPKG_ROOT' is not a usable vcpkg; using the repository default"
    }
}

$sdkStage = Join-Path $BuildDir "sdk"
$apiHeader = Join-Path $moduleRoot "Raster/Include/Raster/RasterApi.h"
$consumerCRoot = Join-Path $moduleRoot "Tools/sdk-consumer-c"
$consumerCppRoot = Join-Path $moduleRoot "Tools/sdk-consumer-cpp"

$script:Failures = New-Object System.Collections.Generic.List[string]
$script:Warnings = New-Object System.Collections.Generic.List[string]

function Write-Step([string]$Text)
{
    Write-Host ""
    Write-Host "== $Text" -ForegroundColor Cyan
}

function Write-Check([bool]$Ok, [string]$Text, [string]$Detail = "")
{
    if ($Ok) {
        Write-Host "  [ok]   $Text" -ForegroundColor Green
        return
    }
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
    if ($Detail) { Write-Host "         $Detail" -ForegroundColor Red }
    $script:Failures.Add($Text)
}

function Write-Note([string]$Text)
{
    Write-Host "  [note] $Text" -ForegroundColor Yellow
    $script:Warnings.Add($Text)
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments, [string]$What)
{
    Write-Host "  > $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    # 原生命令往 stderr 写任何东西（哪怕只是 CMake 的 policy 警告），
    # 在 $ErrorActionPreference = "Stop" 下都会变成终止性错误。
    # 这里临时放宽：真正的失败由退出码判定，警告不该让门禁失败。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $Exe @Arguments 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($code -ne 0) {
        $tail = ($output | Select-Object -Last 40) -join [Environment]::NewLine
        throw "$What failed with exit code $code`n$tail"
    }
    return ($output | Out-String)
}

function Resolve-Dumpbin
{
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    # Look for the MSVC install that CMake itself picked.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $candidate = Get-ChildItem -Path (Join-Path $installPath "VC/Tools/MSVC") -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "Hostx64\\x64" } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($candidate) { return $candidate.FullName }
        }
    }
    throw "dumpbin.exe not found - install the MSVC build tools or run from a Developer Prompt"
}

function Get-DllExports([string]$Dumpbin, [string]$DllPath)
{
    $names = @()
    $output = & $Dumpbin /exports $DllPath 2>&1 | Out-String
    foreach ($line in ($output -split "`r?`n")) {
        # The export table has an "ordinal hint RVA name" layout. Recent dumpbin
        # appends " = name" to the row, so accept both forms:
        #   "   1    0 0001A620 Raster_GetVersion"
        #   "   1    0 0001A620 Raster_GetVersion = Raster_GetVersion"
        $m = [regex]::Match($line, '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+([A-Za-z_][A-Za-z0-9_]*)')
        if ($m.Success) { $names += $m.Groups[1].Value }
    }
    return $names
}

function Get-DllDependencies([string]$Dumpbin, [string]$DllPath)
{
    $names = @()
    $output = & $Dumpbin /dependents $DllPath 2>&1 | Out-String
    $inTable = $false
    foreach ($line in ($output -split "`r?`n")) {
        if ($line -match 'Image has the following dependencies') { $inTable = $true; continue }
        if ($inTable -and $line -match 'Summary') { break }
        if (-not $inTable) { continue }
        $m = [regex]::Match($line, '^\s+([A-Za-z0-9_.\-]+\.dll)\s*$')
        if ($m.Success) { $names += $m.Groups[1].Value }
    }
    return $names
}

# 空数组在 -notcontains 比较里会抛 "Cannot bind argument to parameter
# 'ReferenceObject' because it is null"。统一走这个包装，保证比较两侧
# 都是真正的数组。
function ConvertTo-Array($Value)
{
    if ($null -eq $Value) { return @() }
    return @($Value)
}

function Get-DeclaredExports([string]$HeaderPath)
{
    $text = Get-Content -Raw $HeaderPath
    # RASTER_API <ret> RASTER_CALL <name>(...);  -- [^;] also spans newlines,
    # which matters for the multi-line declarations in the header.
    $found = [regex]::Matches($text, 'RASTER_API\s+[^;]*?RASTER_CALL\s+(\w+)\s*\(')
    $names = @()
    foreach ($m in $found) { $names += $m.Groups[1].Value }
    return ($names | Sort-Object -Unique)
}

function Find-BuiltExecutable([string]$Root, [string]$Name)
{
    $candidates = @(
        (Join-Path $Root "$Name.exe"),
        (Join-Path $Root "$Name"),
        (Join-Path $Root "Release/$Name.exe"),
        (Join-Path $Root "Debug/$Name.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Invoke-Consumer([string]$ExePath, [string]$ExpectedMarker)
{
    Write-Host "  > $ExePath" -ForegroundColor DarkGray
    # 消费程序**故意**会触发一次 OpenCV 的告警（去读一个不存在的文件），
    # 那是预期行为。stderr 上的输出在 $ErrorActionPreference = "Stop" 下
    # 会变成终止性错误，所以这里放宽策略，成败只看退出码。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $ExePath 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    $text = ($output | Out-String)
    Write-Check ($code -eq 0) "consumer exit code 0 ($ExePath)" "exit=$code`n$text"
    Write-Check ($text -match [regex]::Escape($ExpectedMarker)) "consumer printed '$ExpectedMarker'"
    return $text
}

# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Raster SDK verification" -ForegroundColor Cyan
Write-Host "  module:  $moduleRoot"
Write-Host "  build:   $BuildDir"
Write-Host "  vcpkg:   $(if ($VcpkgDir) { $VcpkgDir } else { '<Config.cmake default>' })"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "  cleaning $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

$dumpbin = Resolve-Dumpbin
Write-Host "  dumpbin: $dumpbin"

# ---------------------------------------------------------------------------
Write-Step "1/6 standalone build and install"

$configureArgs = @(
    "-S", $moduleRoot,
    "-B", $BuildDir,
    "-DBUILD_RASTER_EXAMPLES=OFF",
    "-DBUILD_RASTER_TESTS=OFF",
    # 自包含交付：把 OpenCV 运行时一并装到 bin/。
    # 客户拿到这个目录就能直接跑，不需要再自己找 opencv_*.dll。
    "-DRASTER_INSTALL_OPENCV_RUNTIME=ON"
)
if ($VcpkgDir) { $configureArgs += "-DVCPKG_DIR=$VcpkgDir" }

try
{
    Invoke-Checked "cmake" ($configureArgs + @("-A", $Architecture)) "configure" | Out-Null
}
catch
{
    # -A is only valid for Visual Studio generators; retry for Ninja/single-config.
    Write-Note "configure with -A $Architecture failed, retrying without an explicit platform"
    if (Test-Path (Join-Path $BuildDir "CMakeCache.txt")) {
        Remove-Item -Recurse -Force $BuildDir
    }
    Invoke-Checked "cmake" $configureArgs "configure (no platform)" | Out-Null
}

Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Release", "--target", "Raster") "build Release" | Out-Null
Invoke-Checked "cmake" @("--install", $BuildDir, "--config", "Release", "--prefix", $sdkStage) "install Release" | Out-Null

$builtConfigs = @("Release")
if (-not $SkipDebug) {
    Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Debug", "--target", "Raster") "build Debug" | Out-Null
    Invoke-Checked "cmake" @("--install", $BuildDir, "--config", "Debug", "--prefix", $sdkStage) "install Debug" | Out-Null
    $builtConfigs += "Debug"
}
Write-Host "  [ok]   build + install completed ($($builtConfigs -join ', '))" -ForegroundColor Green

# ---------------------------------------------------------------------------
Write-Step "2/6 installed layout"

$includeDir = Join-Path $sdkStage "include/Raster"
$libDir = Join-Path $sdkStage "lib"
$binDir = Join-Path $sdkStage "bin"
$cmakeDir = Join-Path $sdkStage "lib/cmake/Raster"

foreach ($required in @(
    (Join-Path $includeDir "RasterApi.h"),
    (Join-Path $includeDir "RasterTypes.h"),
    (Join-Path $includeDir "RasterExport.h"),
    (Join-Path $includeDir "RasterCpp.h"),
    (Join-Path $libDir "Raster.lib"),
    (Join-Path $binDir "Raster.dll"),
    (Join-Path $cmakeDir "RasterConfig.cmake"),
    (Join-Path $cmakeDir "RasterConfigVersion.cmake"),
    (Join-Path $cmakeDir "RasterTargets.cmake")
)) {
    Write-Check (Test-Path $required) "installed: $($required.Replace($sdkStage, ''))"
}

if (-not $SkipDebug) {
    Write-Check (Test-Path (Join-Path $libDir "Raster_d.lib")) "installed: lib/Raster_d.lib (Debug import library)"
    Write-Check (Test-Path (Join-Path $binDir "Raster_d.dll")) "installed: bin/Raster_d.dll (Debug runtime)"
}

$publicHeaders = @(Get-ChildItem -Path (Join-Path $sdkStage "include") -Recurse -File -ErrorAction SilentlyContinue)
Write-Check ($publicHeaders.Count -eq 4) "include/ exposes exactly 4 public headers" `
    "found: $(($publicHeaders | ForEach-Object { $_.Name }) -join ', ')"

# The internal header (Src/Core/RasterInternal.h) and any OpenCV header must
# never leak into the SDK: a consumer that sees them will start depending on
# OpenCV, which is exactly the coupling this module is designed to avoid.
$leaked = @($publicHeaders | Where-Object { $_.Name -eq "RasterInternal.h" -or $_.Name -match "opencv" })
Write-Check ($leaked.Count -eq 0) "include/ contains no internal headers" `
    "leaked: $(($leaked | ForEach-Object { $_.Name }) -join ', ')"

# bin/ 允许出现 Raster 自身与 OpenCV 运行时（自包含交付）；
# 除此之外的 DLL 一律不允许——那意味着 SDK 悄悄长出了别的依赖。
# 示例源码随包分发：客户拿到包就能直接编译第一个程序。
$examplesDir = Join-Path $sdkStage "share/doc/Raster/examples"
foreach ($example in @("Example_Basic.cpp", "Example_LaserCut.cpp", "Example_Pipeline.cpp",
                        "Example_CApi.c")) {
    Write-Check (Test-Path (Join-Path $examplesDir $example)) "installed: share/doc/Raster/examples/$example"
}

# 第三方归属声明：随包分发 OpenCV 二进制时必须带。
Write-Check (Test-Path (Join-Path $sdkStage "share/doc/Raster/NOTICE.txt")) `
    "installed: share/doc/Raster/NOTICE.txt (third-party attribution)"

$dlls = @(Get-ChildItem -Path $binDir -File -Filter *.dll -ErrorAction SilentlyContinue)
# 允许的第三方运行时：OpenCV 自己的模块，以及它依赖的编解码库。
$allowedRuntimePrefixes = @("opencv_", "libwebp", "libpng", "jpeg", "tiff", "zlib", "libsharpyuv", "liblzma", "libprotobuf", "abseil_")
$unexpectedDlls = @($dlls | Where-Object {
    $name = $_.Name
    if ($name -eq "Raster.dll" -or $name -eq "Raster_d.dll") { return $false }
    foreach ($prefix in $allowedRuntimePrefixes) {
        if ($name.StartsWith($prefix)) { return $false }
    }
    return $true
})
Write-Check ($unexpectedDlls.Count -eq 0) "bin/ contains only Raster + the OpenCV runtime" `
    "unexpected: $(($unexpectedDlls | ForEach-Object { $_.Name }) -join ', ')"

$opencvDlls = @($dlls | Where-Object { $_.Name.StartsWith("opencv_") })
Write-Check ($opencvDlls.Count -gt 0) "bin/ ships the OpenCV runtime (self-contained delivery)" `
    "found: $(($opencvDlls | ForEach-Object { $_.Name }) -join ', ')"

# ---------------------------------------------------------------------------
Write-Step "3/6 exported symbols vs RasterApi.h"

$declared = @(ConvertTo-Array (Get-DeclaredExports $apiHeader))
Write-Check ($declared.Count -gt 0) "parsed $($declared.Count) RASTER_API declarations from the header"

$releaseDll = Join-Path $binDir "Raster.dll"
$releaseExports = @(ConvertTo-Array (Get-DllExports $dumpbin $releaseDll))
$missing = @($declared | Where-Object { $releaseExports -notcontains $_ })
$unexpected = @($releaseExports | Where-Object { $declared -notcontains $_ })

Write-Check ($missing.Count -eq 0) "every declared function is exported" `
    "missing: $($missing -join ', ')"
Write-Check ($unexpected.Count -eq 0) "no undeclared symbol is exported" `
    "unexpected: $($unexpected -join ', ')"

# The whole point of the POD-only contract: no C++ mangled name may escape.
# A "?..." export means a class / namespace-scope symbol with a non-trivial
# destructor or a template instantiation leaked into the DLL.
# 注意：不能用 -like "?*" —— PowerShell 的 ? 是"任意单个字符"通配符，
# 那样任何名字都会匹配。必须用字面量 StartsWith。
$mangled = @($releaseExports | Where-Object { $_.StartsWith("?") })
Write-Check ($mangled.Count -eq 0) "no C++ mangled symbol is exported" `
    "mangled: $($mangled -join ', ')"

if (-not $SkipDebug) {
    $debugExports = @(ConvertTo-Array (Get-DllExports $dumpbin (Join-Path $binDir "Raster_d.dll")))
    $diff = @(Compare-Object ($releaseExports | Sort-Object) ($debugExports | Sort-Object))
    Write-Check ($diff.Count -eq 0) "Debug and Release export the same symbol set" `
        "difference: $(($diff | ForEach-Object { "$($_.InputObject) ($($_.SideIndicator))" }) -join ', ')"
}

# ---------------------------------------------------------------------------
Write-Step "4/6 runtime dependencies"

$dependencies = @(ConvertTo-Array (Get-DllDependencies $dumpbin $releaseDll))
Write-Host "  imports: $($dependencies -join ', ')" -ForegroundColor DarkGray

foreach ($required in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "kernel32.dll")) {
    $hit = @($dependencies | Where-Object { $_.ToLower() -eq $required })
    Write-Check ($hit.Count -gt 0) "imports $required" "imports: $($dependencies -join ', ')"
}

# OpenCV is expected: the module is built on top of it. But nothing else may
# appear -- a stray Qt/spdlog import means the SDK grew a dependency the
# customer would have to deploy for no reason.
$forbiddenPatterns = @("qt6", "qt5", "spdlog", "fmt.dll", "log.dll", "clipper", "nesting", "vision")
foreach ($pattern in $forbiddenPatterns) {
    $hit = @($dependencies | Where-Object { $_.ToLower().Contains($pattern) })
    Write-Check ($hit.Count -eq 0) "does not import anything matching '$pattern'" `
        "imported: $($hit -join ', ')"
}

$opencvHits = @($dependencies | Where-Object { $_.ToLower().StartsWith("opencv_") })
Write-Check ($opencvHits.Count -gt 0) "imports the OpenCV runtime it was built against" `
    "imported: $($dependencies -join ', ')"

# A Release binary that pulls in the Debug CRT means the shipped package mixes configs.
$debugCrt = @($dependencies | Where-Object { $_.ToLower() -match "(msvcp140d|vcruntime140d|ucrtbased)" })
Write-Check ($debugCrt.Count -eq 0) "Release binary does not import the Debug CRT" `
    "imported: $($debugCrt -join ', ')"

# ---------------------------------------------------------------------------
Write-Step "5/6 external consumers (C and C++)"

$consumerCBuild = Join-Path $BuildDir "consumer-c"
Invoke-Checked "cmake" @("-S", $consumerCRoot, "-B", $consumerCBuild, "-DRASTER_SDK_ROOT=$sdkStage") "configure C consumer" | Out-Null
Invoke-Checked "cmake" @("--build", $consumerCBuild, "--config", "Release") "build C consumer" | Out-Null
$consumerCExe = Find-BuiltExecutable $consumerCBuild "consumer_c"
Write-Check ($null -ne $consumerCExe) "C consumer executable produced"
if ($consumerCExe) { Invoke-Consumer $consumerCExe "consumer-c ok" | Out-Null }

$consumerCppBuild = Join-Path $BuildDir "consumer-cpp"
Invoke-Checked "cmake" @("-S", $consumerCppRoot, "-B", $consumerCppBuild, "-DCMAKE_PREFIX_PATH=$sdkStage") "configure C++ consumer (find_package)" | Out-Null
Invoke-Checked "cmake" @("--build", $consumerCppBuild, "--config", "Release") "build C++ consumer" | Out-Null
$consumerCppExe = Find-BuiltExecutable $consumerCppBuild "consumer_cpp"
Write-Check ($null -ne $consumerCppExe) "C++ consumer executable produced"
if ($consumerCppExe) { Invoke-Consumer $consumerCppExe "consumer-cpp ok" | Out-Null }

# ---------------------------------------------------------------------------
if ($Package) {
    Write-Step "6/6 deliverable archive"

    $moduleVersion = "unknown"
    $typesHeader = Join-Path $moduleRoot "Raster/Include/Raster/RasterTypes.h"
    if (Test-Path $typesHeader) {
        $versionText = Get-Content -Raw $typesHeader
        $maj = [regex]::Match($versionText, 'RASTER_VERSION_MAJOR\s+(\d+)').Groups[1].Value
        $min = [regex]::Match($versionText, 'RASTER_VERSION_MINOR\s+(\d+)').Groups[1].Value
        $pat = [regex]::Match($versionText, 'RASTER_VERSION_PATCH\s+(\d+)').Groups[1].Value
        if ($maj -and $min -and $pat) { $moduleVersion = "$maj.$min.$pat" }
    }

    if (-not $PackagePath) {
        $PackagePath = Join-Path $BuildDir "Raster-$moduleVersion-$Architecture-windows.zip"
    }
    $packageDir = Split-Path -Parent $PackagePath
    if (-not (Test-Path $packageDir)) { New-Item -ItemType Directory -Force -Path $packageDir | Out-Null }

    # 工具链信息：cmake -P 脚本模式取不到，从配置产生的缓存里读。
    $generator = "unknown"
    $cacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $cacheText = Get-Content -Raw $cacheFile
        $m = [regex]::Match($cacheText, '(?m)^CMAKE_GENERATOR:INTERNAL=(.+)$')
        if ($m.Success) { $generator = $m.Groups[1].Value.Trim() }
    }
    $hostInfo = "$([System.Environment]::OSVersion.Platform) $env:PROCESSOR_ARCHITECTURE"

    # 打包脚本是跨平台的 CMake 脚本：Windows / Linux / macOS 出同一个结果。
    $packageScript = Join-Path $moduleRoot "Tools/package/PackageSdk.cmake"
    Invoke-Checked "cmake" @(
        "-DSDK_ROOT=$sdkStage",
        "-DSDK_OUTPUT=$PackagePath",
        "-DSDK_ARCH=$Architecture-windows",
        "-DSDK_VERSION=$moduleVersion",
        "-DSDK_GENERATOR=$generator",
        "-DSDK_HOST=$hostInfo",
        "-P", $packageScript
    ) "package the SDK" | Out-Null

    Write-Check (Test-Path $PackagePath) "archive produced: $PackagePath"
    if (Test-Path $PackagePath) {
        $zipSize = (Get-Item $PackagePath).Length
        Write-Host ("  [ok]   archive size: {0:N1} MB" -f ($zipSize / 1MB)) -ForegroundColor Green

        # 包内必须自带清单与身份信息：客户拿到手就能独立核对。
        $entries = (Invoke-Checked "cmake" @("-E", "tar", "tf", $PackagePath) "list the archive") | Out-String
        foreach ($required in @("MANIFEST.txt", "VERSION.txt",
                                "include/Raster/RasterApi.h",
                                "include/Raster/RasterTypes.h",
                                "include/Raster/RasterExport.h",
                                "include/Raster/RasterCpp.h",
                                "lib/cmake/Raster/RasterConfig.cmake",
                                "share/doc/Raster/NOTICE.txt")) {
            $normalized = $entries.Replace("\", "/")
            Write-Check ($normalized.Contains($required)) "archive contains: $required"
        }
    }
}

# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "------------------------------------------------------------------"
if ($script:Warnings.Count -gt 0) {
    Write-Host "notes ($($script:Warnings.Count)):" -ForegroundColor Yellow
    foreach ($warning in $script:Warnings) { Write-Host "  - $warning" -ForegroundColor Yellow }
}
if ($script:Failures.Count -gt 0) {
    Write-Host "FAILED: $($script:Failures.Count) check(s)" -ForegroundColor Red
    foreach ($failure in $script:Failures) { Write-Host "  - $failure" -ForegroundColor Red }
    Write-Host "SDK artifacts kept at: $sdkStage" -ForegroundColor Red
    exit 1
}

Write-Host "ALL SDK CHECKS PASSED" -ForegroundColor Green
Write-Host "SDK artifacts: $sdkStage"
exit 0
