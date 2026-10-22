param(
    [Parameter(Mandatory = $true)]
    [string]$ClientId,

    [string]$BuildDir = "",
    [string]$ClientsRoot = "",
    [string]$OutDir = "",
    [switch]$NoZip,
    [switch]$NoShortcut
)

$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if (-not $ClientsRoot) { $ClientsRoot = Join-Path $repoRoot "Clients" }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot "dist" }

$clientDir = Join-Path $ClientsRoot $ClientId
$configPath = Join-Path $clientDir ("configs\{0}.json" -f $ClientId)
$brandDir = Join-Path $clientDir ("branding\{0}" -f $ClientId)
$brandPath = Join-Path $brandDir "brand.json"

if (-not (Test-Path -LiteralPath $configPath)) {
    throw "Client config not found: $configPath"
}
if (-not (Test-Path -LiteralPath $brandPath)) {
    throw "Client brand not found: $brandPath"
}

$utf8 = New-Object System.Text.UTF8Encoding($false)

function Read-JsonFile([string]$Path) {
    $text = [IO.File]::ReadAllText($Path)
    try {
        return $text | ConvertFrom-Json
    }
    catch {
        throw "Invalid JSON: $Path`n$($_.Exception.Message)"
    }
}

$config = Read-JsonFile $configPath
if ($config.meta.clientId -and $config.meta.clientId -ne $ClientId) {
    throw "Config meta.clientId '$($config.meta.clientId)' does not match '$ClientId'"
}
$brand = Read-JsonFile $brandPath
if ($brand.clientId -and $brand.clientId -ne $ClientId) {
    throw "brand.json clientId '$($brand.clientId)' does not match '$ClientId'"
}
if (-not $brand.productName) {
    throw "brand.json productName is required"
}

if (-not $BuildDir) {
    $candidates = @(
        (Join-Path $repoRoot "build\bin_Qt6\Release"),
        (Join-Path $repoRoot "build\bin_Qt6\Debug"),
        (Join-Path $repoRoot "build\bin_Qt6")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $BuildDir = $candidate
            break
        }
    }
}
if (-not $BuildDir -or -not (Test-Path -LiteralPath $BuildDir)) {
    throw "Build output not found. Pass -BuildDir <path to bin_Qt6/Release>."
}

$exe = Get-ChildItem -LiteralPath $BuildDir -Filter "*.exe" -File |
    Where-Object { $_.Name -notmatch "^Qt" } |
    Select-Object -First 1
if (-not $exe) {
    throw "No application executable found in $BuildDir"
}

$dist = Join-Path $OutDir $ClientId
if (Test-Path -LiteralPath $dist) {
    Remove-Item -LiteralPath $dist -Recurse -Force
}
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item -Path (Join-Path $BuildDir "*") -Destination $dist -Recurse -Force

$distConfigs = Join-Path $dist "configs"
New-Item -ItemType Directory -Path $distConfigs -Force | Out-Null
Copy-Item -LiteralPath $configPath -Destination $distConfigs -Force
$baseConfig = Join-Path $repoRoot "Main\Src\UI\ClientConfig\configs\base.json"
if (Test-Path -LiteralPath $baseConfig) {
    Copy-Item -LiteralPath $baseConfig -Destination $distConfigs -Force
}

$distBrand = Join-Path $dist ("branding\{0}" -f $ClientId)
New-Item -ItemType Directory -Path (Split-Path -Parent $distBrand) -Force | Out-Null
Copy-Item -LiteralPath $brandDir -Destination $distBrand -Recurse -Force

$pluginSource = Join-Path (Join-Path $BuildDir "plugins\clients") $ClientId
$pluginTarget = Join-Path $dist ("plugins\clients\{0}" -f $ClientId)
if (Test-Path -LiteralPath $pluginSource) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $pluginTarget) -Force | Out-Null
    Copy-Item -LiteralPath $pluginSource -Destination $pluginTarget -Recurse -Force
}
else {
    Write-Warning "Plugin output not found for '$ClientId': $pluginSource (client will run without plugin panels)"
}

[IO.File]::WriteAllText((Join-Path $dist "client.id"), $ClientId + "`r`n", $utf8)

if (-not $NoShortcut) {
    $shortcutName = ($brand.productName -replace '[\\/:*?"<>|]', '') + ".lnk"
    $shortcutPath = Join-Path $dist $shortcutName
    $iconValue = if ($brand.appIcon) { $brand.appIcon } else { "" }
    $iconPath = $exe.FullName
    if ($iconValue) {
        $candidate = if ([IO.Path]::IsPathRooted($iconValue)) { $iconValue } else { Join-Path $brandDir $iconValue }
        if (Test-Path -LiteralPath $candidate) { $iconPath = [IO.Path]::GetFullPath($candidate) }
    }

    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe.FullName
    $shortcut.WorkingDirectory = $dist
    $shortcut.IconLocation = "$iconPath,0"
    $shortcut.Description = $brand.productName
    $shortcut.Save()
}

if (-not $NoZip) {
    $zipPath = Join-Path $OutDir ("{0}-{1}.zip" -f $ClientId, (Get-Date -Format "yyyyMMdd"))
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zipPath -Force
    Write-Host "Package: $zipPath" -ForegroundColor Green
}

Write-Host "Client package ready: $dist" -ForegroundColor Green
Write-Host "  product : $($brand.productName)"
Write-Host "  client  : $ClientId"
Write-Host "  exe     : $($exe.Name)"
