param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath (Join-Path $Root 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw '无效的发布版本号。' }
$dist = Join-Path $Root 'dist'
$installer = Join-Path $dist "FastFile-Setup-$version.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw '缺少对应版本的安装包。' }
$changelog = Get-Content -LiteralPath (Join-Path $Root 'CHANGELOG.md') -Raw -Encoding UTF8
$heading = '(?m)^### ' + [regex]::Escape($version) + '(?:[（(\s]|$)'
$match = [regex]::Match($changelog, $heading)
if (-not $match.Success) { throw '更新日志缺少本版本记录。' }
$section = $changelog.Substring($match.Index)
$next = [regex]::Match($section.Substring(4), '(?m)^#{2,3} ')
if ($next.Success) { $section = $section.Substring(0, $next.Index + 4) }
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'), "$hash  FastFile-Setup-$version.exe`n", $utf8)
$notes = "Windows x64 预发布版。下载下面的安装包；Source code 压缩包是源码。`n`n" +
    "本安装包由 GitHub Actions 从发布提交构建，自动回归、安装包内容与进程归属检查通过后发布。`n`n" +
    "实际第三方软件菜单、安装／卸载及现场视觉效果仍需人工验收；本版保留预发布标记。`n`n" +
    $section.Trim().Replace('，未发布', '') + "`n`n安装包 SHA256：``$hash```n"
[System.IO.File]::WriteAllText((Join-Path $dist 'RELEASE_NOTES.md'), $notes, $utf8)
Write-Host "Prepared FastFile $version ($hash)"
