# Launch SanYiCAD, auto-click trial dialog, check survival
# Chinese strings built from code points (PS5 reads ps1 as ANSI)
param([int]$WaitSeconds = 15)

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

# "shi yong qi" (trial period)
$titleZh = [string]([char]0x8BD5) + [char]0x7528 + [char]0x671F
# "ji xu shi yong" (continue trial)
$btnZh = [string]([char]0x7EE7) + [char]0x7EED + [char]0x8BD5 + [char]0x7528

$exe = "c:\Users\xx\Documents\Cpp\CAD\build\bin_Qt6\Release\SanYiCAD.exe"
$outFile = "c:\Users\xx\Documents\Cpp\CAD\crash-stderr.txt"
if (Test-Path $outFile) { Remove-Item $outFile -Force }

Start-Process -FilePath $exe -RedirectStandardError $outFile

$clicked = $false
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Seconds 1
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $win = $null
    foreach ($name in @($titleZh, "Trial Period")) {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, $name)
        $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
        if ($win) { break }
    }
    if ($win) {
        foreach ($btnName in @($btnZh, "Continue Trial", "Continue")) {
            $cond2 = New-Object System.Windows.Automation.PropertyCondition(
                [System.Windows.Automation.AutomationElement]::NameProperty, $btnName)
            $btn = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond2)
            if ($btn) {
                try {
                    $pattern = $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                    $pattern.Invoke()
                    Write-Host "CLICKED trial dialog button"
                    $clicked = $true
                } catch {
                    Write-Host "CLICK FAILED: $_"
                }
                break
            }
        }
        if ($clicked) { break }
    }
}
if (-not $clicked) { Write-Host "WARNING: trial dialog not found/clicked" }

Start-Sleep -Seconds $WaitSeconds
$p = Get-Process "SanYiCAD" -ErrorAction SilentlyContinue
if ($p) {
    Write-Host "RESULT: ALIVE PID=$($p.Id)"
    $p.Kill()
} else {
    Write-Host "RESULT: CRASHED"
}
if (Test-Path $outFile) {
    $hits = Select-String -Path $outFile -Pattern "VEH: exception|terminate called|CRASH" | Select-Object -First 3
    if ($hits) { $hits | ForEach-Object { Write-Host $_.Line } }
}
