#Requires -Version 5.1
<#
.SYNOPSIS
    April CAD Windows orchestration script.

.DESCRIPTION
    Builds the Release target (optional), deploys Qt runtime with windeployqt,
    collects project/vcpkg DLLs, optionally signs binaries, and produces a
    portable ZIP. With -MakeInstaller, an NSIS installer is generated too.

    User-facing output is English; comments are Chinese (project convention).
    Idempotent: only the staging directory inside -OutDir is removed.

.PARAMETER BuildDir
    CMake build directory. Default: auto-detect (repo\build or build-*).

.PARAMETER OutDir
    Output directory. Default: <repo>\dist.

.PARAMETER Version
    Package version. Default: project VERSION from CMakeLists.txt.

.PARAMETER Triplet
    vcpkg triplet, used for logging/validation only. Default: x64-windows.

.PARAMETER Arch
    Target architecture: x64 or x86. Default: x64.

.PARAMETER QtDir
    Qt installation prefix (contains bin\windeployqt.exe). Default: auto-detect.

.PARAMETER SkipBuild
    Reuse existing build artifacts instead of running cmake --build.

.PARAMETER Configuration
    CMake configuration. Default: Release.

.PARAMETER Jobs
    Parallel build jobs (0 = CMake default).

.PARAMETER SignThumbprint
    Certificate thumbprint for signtool. Defaults to $env:APR_WIN_SIGN_THUMBPRINT.
    When neither is present, binaries are not signed and a warning is printed.

.PARAMETER MakeInstaller
    Also generate an NSIS installer (requires makensis).

.NOTES
    Signing credentials can come from environment variables so CI never stores
    secrets in the repository:
      APR_WIN_SIGN_THUMBPRINT   certificate thumbprint in the Windows cert store
      APR_WIN_SIGN_PFX          path to a PFX file (alternative to thumbprint)
      APR_WIN_SIGN_PASSWORD     PFX password
      APR_WIN_SIGN_TIMESTAMP    RFC 3161 timestamp URL override
    Missing credentials only produce a warning; packaging continues.

.EXAMPLE
    .\package-windows.ps1 -SkipBuild -OutDir dist

.EXAMPLE
    .\package-windows.ps1 -Version 2.0.0 -Triplet x64-windows -MakeInstaller
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutDir,
    [string]$Version,
    [string]$Triplet = "x64-windows",
    [ValidateSet("x64", "x86")]
    [string]$Arch = "x64",
    [string]$QtDir,
    [switch]$SkipBuild,
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [int]$Jobs = 0,
    [string]$SignThumbprint,
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [switch]$MakeInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 环境变量作为凭据入口，命令行参数优先；仓库内不保存任何密钥。
if (-not $SignThumbprint) { $SignThumbprint = $env:APR_WIN_SIGN_THUMBPRINT }
$SignPfxPath = $env:APR_WIN_SIGN_PFX
$SignPfxPassword = $env:APR_WIN_SIGN_PASSWORD
if ($env:APR_WIN_SIGN_TIMESTAMP) { $TimestampUrl = $env:APR_WIN_SIGN_TIMESTAMP }

# ----------------------------------------------------------------------------
# 静态配置 / 日志
# ----------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$ConfPath = Join-Path $ScriptDir "package.conf"

function Read-PackageConf {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Missing shared config: $Path"
    }

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
        $parts = $trimmed.Split("=", 2)
        if ($parts.Length -eq 2) {
            $values[$parts[0].Trim()] = $parts[1].Trim()
        }
    }
    return $values
}

function Write-Step { param([string]$Message) Write-Host "[STEP] $Message" -ForegroundColor Cyan }
function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Green }
function Write-Warn { param([string]$Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Stop-Package {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
    exit 1
}

# Windows PowerShell 5.1 没有 $IsWindows，PowerShell 7+ 才有；PS 5.1 只可能运行在 Windows。
$onWindows = $true
if ($PSVersionTable.PSVersion.Major -ge 6) { $onWindows = $IsWindows }
if (-not $onWindows) { Stop-Package "This script must run on Windows." }

$conf = Read-PackageConf $ConfPath
$AppName = $conf["APP_NAME"]
$ExeName = $conf["EXE_NAME"]
$DefaultVersion = $conf["DEFAULT_VERSION"]
if (-not $AppName -or -not $ExeName) { Stop-Package "APP_NAME/EXE_NAME missing in $ConfPath" }

# ----------------------------------------------------------------------------
# 路径 / 版本
# ----------------------------------------------------------------------------
function Resolve-RepoPath {
    param([string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Find-BuildDir {
    param([string]$Explicit)

    if ($Explicit) {
        $resolved = Resolve-RepoPath $Explicit
        if (-not (Test-Path $resolved)) { Stop-Package "Build directory does not exist: $resolved" }
        return $resolved
    }

    $candidates = @(Join-Path $RepoRoot "build")
    $candidates += @(Get-ChildItem -Path $RepoRoot -Directory -Filter "build-*" -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })

    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate "CMakeCache.txt")) { return $candidate }
    }
    Stop-Package "No CMake build directory found. Pass -BuildDir <path>."
}

function Find-BinDir {
    param([string]$Root, [string]$Config)

    $dirs = @(Get-ChildItem -Path $Root -Directory -Filter "bin_Qt*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending)
    foreach ($dir in $dirs) {
        $withConfig = Join-Path $dir.FullName $Config
        if (Test-Path $withConfig) { return $withConfig }
        if (Test-Path (Join-Path $dir.FullName "$ExeName.exe")) { return $dir.FullName }
    }
    return $null
}

function Get-RepoVersion {
    $cmakeLists = Join-Path $RepoRoot "CMakeLists.txt"
    if (Test-Path $cmakeLists) {
        $match = Select-String -LiteralPath $cmakeLists -Pattern 'VERSION ([0-9]+\.[0-9]+\.[0-9]+)' |
            Select-Object -First 1
        if ($match) { return $match.Matches[0].Groups[1].Value }
    }
    return $DefaultVersion
}

function Read-CacheValue {
    param([string]$Root, [string]$Key)

    $cache = Join-Path $Root "CMakeCache.txt"
    if (-not (Test-Path $cache)) { return $null }
    $line = Select-String -LiteralPath $cache -Pattern "^$([regex]::Escape($Key)):.*=(.*)$" |
        Select-Object -First 1
    if ($line) { return $line.Matches[0].Groups[1].Value }
    return $null
}

function Resolve-QtDir {
    param([string]$Explicit, [string]$Root)

    if ($Explicit) {
        $resolved = Resolve-RepoPath $Explicit
        if (-not (Test-Path $resolved)) { Stop-Package "Qt directory does not exist: $resolved" }
        return $resolved
    }

    foreach ($candidate in @($env:QT_DIR, $env:QTDIR)) {
        if ($candidate -and (Test-Path $candidate)) { return (Resolve-Path $candidate).Path }
    }

    $fromCache = Read-CacheValue $Root "Qt_INSTALL_DIR"
    if ($fromCache -and (Test-Path $fromCache)) { return $fromCache }

    $roots = @("C:\Qt", "$env:USERPROFILE\Qt")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $versions = @(Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.' } | Sort-Object Name -Descending)
        foreach ($ver in $versions) {
            $kits = @(Get-ChildItem -Path $ver.FullName -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like "*_64" -or $_.Name -like "msvc*" } |
                Sort-Object Name -Descending)
            foreach ($kit in $kits) {
                if (Test-Path (Join-Path $kit.FullName "bin\windeployqt.exe")) {
                    return $kit.FullName
                }
            }
        }
    }

    return $null
}

function Remove-Safe {
    param([string]$Target, [string]$AllowedRoot)

    $fullTarget = [IO.Path]::GetFullPath($Target)
    $fullRoot = [IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\')
    if (-not $fullTarget.StartsWith($fullRoot + "\")) {
        Stop-Package "Refusing to remove path outside output directory: $fullTarget"
    }
    if (Test-Path $fullTarget) { Remove-Item -LiteralPath $fullTarget -Recurse -Force }
}

$BuildRoot = Find-BuildDir $BuildDir
$VersionToUse = if ($Version) { $Version } else { Get-RepoVersion }
$OutRoot = if ($OutDir) { Resolve-RepoPath $OutDir } else { Join-Path $RepoRoot "dist" }
New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null

Write-Info "Repository : $RepoRoot"
Write-Info "Build dir  : $BuildRoot"
Write-Info "Version    : $VersionToUse"
Write-Info "Triplet    : $Triplet"
Write-Info "Arch       : $Arch"

# ----------------------------------------------------------------------------
# 1. 构建
# ----------------------------------------------------------------------------
if ($SkipBuild) {
    Write-Info "Skipping build (-SkipBuild)"
} else {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) { Stop-Package "cmake not found in PATH. Install CMake 4.3+ or use -SkipBuild." }

    Write-Step "Building $Configuration target: $ExeName"
    if (-not (Test-Path (Join-Path $BuildRoot "CMakeCache.txt"))) {
        & cmake -S $RepoRoot -B $BuildRoot -G "Visual Studio 17 2022" -A $Arch `
            "-DCMAKE_BUILD_TYPE=$Configuration"
        if ($LASTEXITCODE -ne 0) { Stop-Package "CMake configure failed." }
    }

    $buildArgs = @("--build", $BuildRoot, "--config", $Configuration, "--target", $ExeName)
    if ($Jobs -gt 0) { $buildArgs += @("--parallel", "$Jobs") }
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) {
        Stop-Package "Build failed. Ensure Qt, vcpkg dependencies and enabled submodules are available, or use -SkipBuild."
    }
}

$BinDir = Find-BinDir $BuildRoot $Configuration
if (-not $BinDir) { Stop-Package "No bin_Qt* output directory found under $BuildRoot" }
$AppExe = Join-Path $BinDir "$ExeName.exe"
if (-not (Test-Path $AppExe)) { Stop-Package "Application executable not found: $AppExe" }

$QtPrefix = Resolve-QtDir $QtDir $BuildRoot
if (-not $QtPrefix) {
    Stop-Package "Qt installation not found. Pass -QtDir, set QTDIR, or configure CMake first."
}
$WinDeployQt = Join-Path $QtPrefix "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) { Stop-Package "windeployqt.exe not found under $QtPrefix\bin" }

$BaseName = "$AppName-$VersionToUse-windows-$Arch"
$WorkDir = Join-Path $OutRoot "windows"
$StageDir = Join-Path $WorkDir $AppName
$StagedExe = Join-Path $StageDir "$ExeName.exe"

Write-Info "Binary dir : $BinDir"
Write-Info "Qt prefix  : $QtPrefix"
Write-Info "Output     : $OutRoot"

# ----------------------------------------------------------------------------
# 2. staging
# ----------------------------------------------------------------------------
Write-Step "Staging portable application directory"
Remove-Safe $WorkDir $OutRoot
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item -LiteralPath $AppExe -Destination $StageDir -Force

# 工程与 vcpkg 运行时 DLL：排除测试 / 基准产物
# 注意：-Include 在不带通配路径时会被忽略，这里用扩展名过滤更可靠。
Get-ChildItem -LiteralPath $BinDir -File -ErrorAction SilentlyContinue |
    Where-Object { ($_.Extension -in @(".dll", ".pyd")) -and ($_.Name -notmatch '^(gtest|gmock)') } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $StageDir -Force }

foreach ($extra in @("plugins", "resources", "translations", "Python", "tools")) {
    $source = Join-Path $BinDir $extra
    if (Test-Path $source) {
        Write-Info "Staging runtime directory: $extra"
        Copy-Item -LiteralPath $source -Destination $StageDir -Recurse -Force
    }
}

Write-Step "Deploying Qt runtime with windeployqt"
& $WinDeployQt --release --verbose 1 $StagedExe
if ($LASTEXITCODE -ne 0) { Stop-Package "windeployqt failed (exit code $LASTEXITCODE)." }

# ----------------------------------------------------------------------------
# 3. 可选签名
# ----------------------------------------------------------------------------
function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path $kitsRoot) {
        $candidate = Get-ChildItem -Path $kitsRoot -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    return $null
}

if ($SignThumbprint -or $SignPfxPath) {
    $signtool = Find-SignTool
    if (-not $signtool) {
        Stop-Package "signtool.exe not found. Install the Windows SDK or run without signing credentials."
    }

    $signArgs = @("sign", "/fd", "sha256", "/tr", $TimestampUrl, "/td", "sha256")
    if ($SignThumbprint) {
        $signArgs += @("/sha1", $SignThumbprint)
        Write-Step "Signing binaries (thumbprint: $SignThumbprint)"
    } else {
        if (-not (Test-Path -LiteralPath $SignPfxPath)) {
            Stop-Package "PFX file not found: $SignPfxPath"
        }
        $signArgs += @("/f", $SignPfxPath)
        if ($SignPfxPassword) { $signArgs += @("/p", $SignPfxPassword) }
        Write-Step "Signing binaries (PFX: $SignPfxPath)"
    }

    Get-ChildItem -LiteralPath $StageDir -Recurse -File |
        Where-Object { $_.Extension -in @(".exe", ".dll", ".pyd") } |
        ForEach-Object {
            & $signtool @signArgs $_.FullName
            if ($LASTEXITCODE -ne 0) { Stop-Package "signtool failed on $($_.FullName)" }
        }
} else {
    Write-Warn "Code signing skipped (no thumbprint or PFX supplied)."
    Write-Warn "Set APR_WIN_SIGN_THUMBPRINT or APR_WIN_SIGN_PFX to sign release artifacts."
}

# ----------------------------------------------------------------------------
# 4. 便携 ZIP
# ----------------------------------------------------------------------------
Write-Step "Creating portable ZIP archive"
$ZipPath = Join-Path $OutRoot "$BaseName.zip"
if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
Compress-Archive -Path $StageDir -DestinationPath $ZipPath -Force
Write-Info "Created: $ZipPath"

# ----------------------------------------------------------------------------
# 5. 可选 NSIS 安装包
# ----------------------------------------------------------------------------
if ($MakeInstaller) {
    $makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if (-not $makensis) {
        Stop-Package "makensis.exe not found. Install NSIS (choco install nsis) or omit -MakeInstaller."
    }

    Write-Step "Generating NSIS installer"
    $installerPath = Join-Path $OutRoot "$BaseName-setup.exe"
    $nsiPath = Join-Path $WorkDir "$AppName.nsi"
    $version4 = "$VersionToUse.0"

    $nsi = @"
Unicode true
SetCompressor /SOLID lzma
Name "$AppName"
OutFile "$installerPath"
InstallDir `$PROGRAMFILES64\$AppName
RequestExecutionLevel admin
VIProductVersion "$version4"
VIAddVersionKey "ProductName" "$AppName"
VIAddVersionKey "FileVersion" "$VersionToUse"
VIAddVersionKey "ProductVersion" "$VersionToUse"
VIAddVersionKey "CompanyName" "April Technology"
VIAddVersionKey "FileDescription" "$AppName installer"

Page directory
Page instfiles

Section "Install"
  SetOutPath `$INSTDIR
  File /r "$StageDir\*.*"
  CreateShortCut "`$DESKTOP\$AppName.lnk" "`$INSTDIR\$ExeName.exe"
  CreateDirectory "`$SMPROGRAMS\$AppName"
  CreateShortCut "`$SMPROGRAMS\$AppName\$AppName.lnk" "`$INSTDIR\$ExeName.exe"
  WriteUninstaller "`$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName" "DisplayName" "$AppName"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName" "UninstallString" "`$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName" "DisplayVersion" "$VersionToUse"
SectionEnd

Section "Uninstall"
  Delete "`$DESKTOP\$AppName.lnk"
  Delete "`$SMPROGRAMS\$AppName\$AppName.lnk"
  RMDir "`$SMPROGRAMS\$AppName"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
  RMDir /r `$INSTDIR
SectionEnd
"@
    Set-Content -LiteralPath $nsiPath -Value $nsi -Encoding UTF8
    & $makensis.Source $nsiPath
    if ($LASTEXITCODE -ne 0) { Stop-Package "makensis failed (exit code $LASTEXITCODE)." }
    Write-Info "Created: $installerPath"
}

# ----------------------------------------------------------------------------
# 6. 结果
# ----------------------------------------------------------------------------
Write-Info "Windows package completed"
Write-Host ("  {0,-12} {1}" -f "Portable:", $StageDir)
Write-Host ("  {0,-12} {1}" -f "Archive:", $ZipPath)
if ($MakeInstaller) {
    Write-Host ("  {0,-12} {1}" -f "Installer:", $installerPath)
}
Write-Warn "Windows packaging was authored and syntax-checked, but NOT executed on a Windows host."
