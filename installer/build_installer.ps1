# Builds dist\FastFile-Setup-<version>.exe.
#
# The installer is a small C# program (installer\setup.cs) compiled with the .NET
# Framework compiler that ships with Windows; FastFile.exe + the skin folder are
# embedded as resources. No third-party installer toolchain required.
#
#   powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
param(
    [string]$Configuration = 'Release',
    [string]$Version = '1.0.0'
)
$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = Join-Path $root "build\$Configuration"
$dist  = Join-Path $root 'dist'
$stage = Join-Path $dist '_stage'
$payload = Join-Path $stage 'payload'
$exe   = Join-Path $build 'FastFile.exe'
$skin  = Join-Path $build 'skin'

if (-not (Test-Path -LiteralPath $exe))  { throw "找不到 $exe ，请先构建 $Configuration。" }
if (-not (Test-Path -LiteralPath $skin)) { throw "找不到 $skin （皮肤资源未随 exe 一起生成）。" }

$csc = @(
    (Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'),
    (Join-Path $env:SystemRoot 'Microsoft.NET\Framework\v4.0.30319\csc.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $csc) { throw '找不到 .NET Framework 的 csc.exe（Windows 通常自带 4.x）。' }

Write-Host "[1/4] 准备输出目录"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $payload | Out-Null
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Write-Host "[2/4] 收集程序文件"
Copy-Item -LiteralPath $exe -Destination $payload -Force
Copy-Item -LiteralPath $skin -Destination $payload -Recurse -Force
# Never ship editor/build leftovers that may sit in the output folder.
Get-ChildItem -LiteralPath $payload -Recurse -File |
    Where-Object { $_.Name -like '*.bak*' -or $_.Name -like '*.original*' -or $_.Name -like '*.tmp' } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

$files = Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName
$manifest = New-Object System.Collections.Generic.List[string]
$resArgs = New-Object System.Collections.Generic.List[string]
$index = 0
foreach ($f in $files) {
    $rel = $f.FullName.Substring($payload.Length + 1).Replace('\', '/')
    $name = 'ff' + $index
    $manifest.Add(($name + '|' + $rel))
    $resArgs.Add(('/resource:"' + $f.FullName + '",' + $name))
    $index++
}
$manifestPath = Join-Path $stage 'manifest.txt'
[System.IO.File]::WriteAllText($manifestPath, (($manifest -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
$resArgs.Add(('/resource:"' + $manifestPath + '",ff_manifest'))

Write-Host ("      嵌入 {0} 个文件" -f $files.Count)

Write-Host "[3/4] 编译安装程序"
$target = Join-Path $dist ("FastFile-Setup-" + $Version + ".exe")
if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }

$versionCs = Join-Path $stage 'version.cs'
[System.IO.File]::WriteAllText($versionCs,
    ("internal static class BuildInfo { public const string Version = `"" + $Version + "`"; }`r`n"),
    (New-Object System.Text.UTF8Encoding($true)))

$cscArgs = @('/nologo', '/target:winexe', '/platform:anycpu', '/optimize+',
             ('/out:"' + $target + '"'),
             '/reference:System.Windows.Forms.dll',
             ('/win32icon:"' + (Join-Path $root 'res\FastFile.ico') + '"')) + $resArgs +
           @(('"' + (Join-Path $root 'installer\setup.cs') + '"'), ('"' + $versionCs + '"'))

& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "csc 编译失败，退出码 $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $target)) { throw "未生成 $target" }

Write-Host "[4/4] 完成"
$mb = [math]::Round((Get-Item -LiteralPath $target).Length / 1MB, 2)
Write-Host ""
Write-Host ("安装程序: {0}  ({1} MB)" -f $target, $mb)
Write-Host "  - 双击安装（当前用户，无需管理员）"
Write-Host "  - 开始菜单 + 卸载项在 设置 > 应用"
