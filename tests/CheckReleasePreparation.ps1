$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ('FastFileReleaseTest-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path (Join-Path $fixture 'dist') -Force | Out-Null
try {
    Set-Content (Join-Path $fixture 'VERSION') '1.2.3' -Encoding ASCII
    Set-Content (Join-Path $fixture 'CHANGELOG.md') "# 日志`n### 1.2.3（未发布）`n- 当前修复`n### 1.2.2`n- 旧版本内容" -Encoding UTF8
    $installer = Join-Path $fixture 'dist/FastFile-Setup-1.2.3.exe'
    [System.IO.File]::WriteAllBytes($installer, [byte[]](1, 2, 3))
    & (Join-Path $repo 'tools/PrepareRelease.ps1') -Root $fixture
    $notes = Get-Content (Join-Path $fixture 'dist/RELEASE_NOTES.md') -Raw -Encoding UTF8
    if ($notes -notmatch '当前修复' -or $notes -match '旧版本内容') { throw '版本日志范围错误。' }
    $sum = Get-Content (Join-Path $fixture 'dist/SHA256SUMS.txt') -Raw
    if ($sum.Trim() -ne ((Get-FileHash $installer).Hash + '  FastFile-Setup-1.2.3.exe')) { throw '校验文件错误。' }
    Set-Content (Join-Path $fixture 'VERSION') '../bad' -Encoding ASCII
    $rejected = $false
    try { & (Join-Path $repo 'tools/PrepareRelease.ps1') -Root $fixture } catch { $rejected = $true }
    if (-not $rejected) { throw '未拒绝非法版本。' }
    Set-Content (Join-Path $fixture 'VERSION') '2.0.0' -Encoding ASCII
    $rejected = $false
    try { & (Join-Path $repo 'tools/PrepareRelease.ps1') -Root $fixture } catch { $rejected = $true }
    if (-not $rejected) { throw '未拒绝缺失安装包。' }
    Copy-Item $installer (Join-Path $fixture 'dist/FastFile-Setup-2.0.0.exe')
    $rejected = $false
    try { & (Join-Path $repo 'tools/PrepareRelease.ps1') -Root $fixture } catch { $rejected = $true }
    if (-not $rejected) { throw '未拒绝缺失版本日志。' }
    Write-Host 'Release preparation tests passed.'
} finally {
    $resolved = [System.IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne [System.IO.Path]::GetTempPath().TrimEnd('\') -or (Split-Path -Leaf $resolved) -notlike 'FastFileReleaseTest-*') { throw 'Unsafe fixture cleanup.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
