# FastFile per-user installer. No admin rights required.
#   - extracts FastFile.zip (FastFile.exe + skin\) into the install folder
#   - creates a Start Menu shortcut
#   - registers an uninstall entry so it shows up in 设置 > 应用
#   - drops the matching uninstaller next to the app
param(
    [string]$InstallDir = '',
    [switch]$Quiet,
    [switch]$NoLaunch
)
$ErrorActionPreference = 'Stop'
$AppName    = 'FastFile'
$AppVersion = '1.0.0'
$Publisher  = 'JINLONG'

function Show-Info([string]$text, [string]$title = $AppName) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($text, $title) | Out-Null
}

function Show-Ask([string]$text, [string]$title = $AppName) {
    Add-Type -AssemblyName System.Windows.Forms
    return ([System.Windows.Forms.MessageBox]::Show(
        $text, $title, [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question) -eq 'Yes')
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $InstallDir) {
    $InstallDir = Join-Path (Join-Path $env:LOCALAPPDATA 'Programs') $AppName
}
$zip = Join-Path $here 'FastFile.zip'
if (-not (Test-Path -LiteralPath $zip)) {
    if (-not $Quiet) { Show-Info "安装包不完整：找不到 FastFile.zip" }
    exit 1
}

try {
    # A running copy would keep FastFile.exe locked.
    Get-Process $AppName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400

    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $InstallDir -Force

    $exe = Join-Path $InstallDir 'FastFile.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "解压后找不到 FastFile.exe" }

    # Uninstaller lives with the app so the registry entry can always find it.
    $uninstSrc = Join-Path $here 'uninstall.ps1'
    $uninstDst = Join-Path $InstallDir 'uninstall.ps1'
    if (Test-Path -LiteralPath $uninstSrc) { Copy-Item -LiteralPath $uninstSrc -Destination $uninstDst -Force }

    # Start Menu shortcut
    $ws = New-Object -ComObject WScript.Shell
    $startMenu = [Environment]::GetFolderPath('Programs')
    $lnkPath = Join-Path $startMenu "$AppName.lnk"
    $lnk = $ws.CreateShortcut($lnkPath)
    $lnk.TargetPath       = $exe
    $lnk.WorkingDirectory = $InstallDir
    $lnk.IconLocation     = "$exe,0"
    $lnk.Description      = 'FastFile 文件管理器'
    $lnk.Save()

    # Add/Remove Programs entry (per-user)
    $regPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
    New-Item -Path $regPath -Force | Out-Null
    $psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $uninstCmd = "`"$psExe`" -NoProfile -ExecutionPolicy Bypass -File `"$uninstDst`" -InstallDir `"$InstallDir`""
    Set-ItemProperty -Path $regPath -Name DisplayName     -Value $AppName
    Set-ItemProperty -Path $regPath -Name DisplayVersion  -Value $AppVersion
    Set-ItemProperty -Path $regPath -Name Publisher       -Value $Publisher
    Set-ItemProperty -Path $regPath -Name DisplayIcon     -Value "$exe,0"
    Set-ItemProperty -Path $regPath -Name InstallLocation -Value $InstallDir
    Set-ItemProperty -Path $regPath -Name UninstallString -Value $uninstCmd
    Set-ItemProperty -Path $regPath -Name QuietUninstallString -Value ($uninstCmd + ' -Quiet')
    Set-ItemProperty -Path $regPath -Name NoModify -Value 1 -Type DWord
    Set-ItemProperty -Path $regPath -Name NoRepair -Value 1 -Type DWord
    try {
        $sizeKb = [int]((Get-ChildItem -LiteralPath $InstallDir -Recurse -File |
            Measure-Object -Property Length -Sum).Sum / 1KB)
        Set-ItemProperty -Path $regPath -Name EstimatedSize -Value $sizeKb -Type DWord
    } catch { }

    if (-not $Quiet) {
        $msg = "FastFile $AppVersion 已安装完成。`n`n安装位置：$InstallDir`n开始菜单：$AppName`n`n是否立即启动？"
        if (-not $NoLaunch -and (Show-Ask $msg)) {
            Start-Process -FilePath $exe -WorkingDirectory $InstallDir
        }
    }
    exit 0
}
catch {
    if (-not $Quiet) { Show-Info ("安装失败：`n" + $_.Exception.Message) }
    exit 1
}
