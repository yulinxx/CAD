param(
    [Parameter(Mandatory = $true)]
    [string]$ClientId,

    [Parameter(Mandatory = $true)]
    [string]$ProductName,

    [string]$CompanyName = "",
    [string]$OrganizationDomain = "",
    [string]$Website = "",
    [string]$SupportEmail = "",
    [string]$ClientsRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if ($ClientId -notmatch '^[a-z][a-z0-9_]*$') {
    throw "ClientId must match ^[a-z][a-z0-9_]*$ (lowercase letters, digits, underscore)"
}

$root = if ($ClientsRoot) { $ClientsRoot } else { Join-Path $PSScriptRoot "..\Clients" }
$root = [IO.Path]::GetFullPath($root)
$template = Join-Path $root "_template"
$target = Join-Path $root $ClientId

if (-not (Test-Path -LiteralPath $template)) {
    throw "Template not found: $template"
}
if ((Test-Path -LiteralPath $target) -and -not $Force) {
    throw "Client directory already exists: $target (use -Force to overwrite)"
}
if (Test-Path -LiteralPath $target) {
    Remove-Item -LiteralPath $target -Recurse -Force
}

Copy-Item -LiteralPath $template -Destination $target -Recurse -Force

$configTemplate = Join-Path $target "configs\client.json"
$configTarget = Join-Path $target ("configs\{0}.json" -f $ClientId)
Move-Item -LiteralPath $configTemplate -Destination $configTarget -Force

$brandTemplate = Join-Path $target "branding\brand.json"
$brandDir = Join-Path $target ("branding\{0}" -f $ClientId)
New-Item -ItemType Directory -Path $brandDir -Force | Out-Null
Move-Item -LiteralPath $brandTemplate -Destination (Join-Path $brandDir "brand.json") -Force

$pascal = ($ClientId -split '_' | ForEach-Object {
    if ($_.Length -eq 0) { "" } else { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) }
}) -join ''

$appId = ($CompanyName + $ProductName) -replace '[\s\-]', ''
if (-not $appId) { $appId = $ProductName -replace '[\s\-]', '' }
if (-not $appId) { $appId = $pascal + "CAD" }

if (-not $CompanyName) { $CompanyName = $ProductName }
if (-not $SupportEmail) { $SupportEmail = "support@example.com" }
if (-not $Website) { $Website = "https://example.com" }
if (-not $OrganizationDomain) { $OrganizationDomain = "example.com" }

$tokens = @{
    "__CLIENT_ID__"           = $ClientId
    "__CLIENT_PASCAL__"       = $pascal
    "__PRODUCT_NAME__"        = $ProductName
    "__COMPANY_NAME__"        = $CompanyName
    "__APP_ID__"              = $appId
    "__ORGANIZATION_DOMAIN__" = $OrganizationDomain
    "__WEBSITE__"             = $Website
    "__SUPPORT_EMAIL__"       = $SupportEmail
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$textExtensions = @(".json", ".cpp", ".h", ".hpp", ".md", ".txt", ".cmake", ".qrc", ".ps1")
$files = Get-ChildItem -LiteralPath $target -Recurse -File |
    Where-Object { $textExtensions -contains $_.Extension -or $_.Name -eq "CMakeLists.txt" }

foreach ($file in $files) {
    $text = [IO.File]::ReadAllText($file.FullName)
    $updated = $text
    foreach ($key in $tokens.Keys) {
        $updated = $updated.Replace($key, $tokens[$key])
    }
    if ($updated -ne $text) {
        [IO.File]::WriteAllText($file.FullName, $updated, $utf8)
    }
}

Write-Host ""
Write-Host "Created client scaffold: $target" -ForegroundColor Green
Write-Host "  config : Clients/$ClientId/configs/$ClientId.json"
Write-Host "  brand  : Clients/$ClientId/branding/$ClientId/brand.json"
Write-Host "  plugin : SanYiClient$pascal -> plugins/clients/$ClientId"
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  cd `"$target`""
Write-Host "  git init"
Write-Host "  git add ."
Write-Host "  git commit -m `"Initial client plugin`""
Write-Host ""
Write-Host "Register as a submodule in the core repository:"
Write-Host "  git submodule add <repo-url> Clients/$ClientId"
