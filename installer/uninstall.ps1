# FastFile uninstaller. Removes the program, the Start Menu shortcut and the
# "应用和功能" entry. User settings under %APPDATA%\FastFile are kept on purpose.
param(
    [string]$InstallDir = '',
    [switch]$Quiet,
    [switch]$Cleanup
)
$ErrorActionPreference = 'Stop'
$AppName = 'FastFile'

# ---- second pass: executed from %TEMP%, deletes the program folder ----------------
if ($Cleanup) {
    Start-Sleep -Milliseconds 600
    if ($InstallDir) {
        for ($i = 0; $i -lt 12; $i++) {
            try {
                if (Test-Path -LiteralPath $InstallDir) {
                    Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction Stop
                }
                break
            } catch {
                Start-Sleep -Milliseconds 400   # the app may still be shutting down
            }
        }
    }
    try { Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue } catch { }
    exit 0
}

function Show-Info([string]$text) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($text, $AppName) | Out-Null
}
function Show-Ask([string]$text) {
    Add-Type -AssemblyName System.Windows.Forms
    return ([System.Windows.Forms.MessageBox]::Show(
        $text, $AppName, [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning) -eq 'Yes')
}

$self = $MyInvocation.MyCommand.Path
$dir  = if ($InstallDir) { $InstallDir } else { Split-Path -Parent $self }

if (-not $Quiet) {
    if (-not (Show-Ask "确定要卸载 FastFile 吗？`n`n$dir`n`n（不会删除 %APPDATA%\FastFile 中的个人设置）")) { exit 0 }
}

Get-Process $AppName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

# Restore opt-in Shell handlers while the installed executable is still available.
$taskRestoreExe = Join-Path $dir 'FastFile.exe'
if (Test-Path -LiteralPath $taskRestoreExe) {
    $taskRestoreProcess = Start-Process -FilePath $taskRestoreExe -ArgumentList '--restore-integration' -WindowStyle Hidden -Wait -PassThru
    if ($taskRestoreProcess.ExitCode -ne 0) {
        if (-not $Quiet) { Show-Info '无法恢复系统打开方式，卸载已停止。请在 FastFile 设置中关闭系统集成后重试。' }
        exit 1
    }
}

# Start Menu shortcut + registry entry
try {
    $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) "$AppName.lnk"
    if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force }
} catch { }
try {
    Remove-Item -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName" -Recurse -Force -ErrorAction SilentlyContinue
} catch { }

# The script itself lives in the install folder, so copy it to TEMP and let that copy
# delete everything (a running script cannot reliably delete its own folder).
$tmp = Join-Path $env:TEMP ("fastfile-uninst-" + [Guid]::NewGuid().ToString('N') + ".ps1")
Copy-Item -LiteralPath $self -Destination $tmp -Force

$args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$tmp`"", '-InstallDir', "`"$dir`"", '-Quiet', '-Cleanup')
Start-Process -FilePath 'powershell.exe' -ArgumentList $args -WindowStyle Hidden | Out-Null

if (-not $Quiet) { Show-Info "FastFile 已卸载。" }
exit 0
