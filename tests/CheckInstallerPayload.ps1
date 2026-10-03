param(
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [Parameter(Mandatory = $true)][string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVersion = (Get-Content -LiteralPath (Join-Path $taskRoot 'VERSION') -Raw).Trim()
$taskInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
$taskBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if ([System.IO.Path]::GetFileName($taskInstaller) -ne "FastFile-Setup-$taskVersion.exe") {
    throw '安装包文件名与 VERSION 不一致。'
}
# Inspect metadata and resource streams without executing the installer entry point.
$taskAssembly = [System.Reflection.Assembly]::LoadFile($taskInstaller)
$taskField = $taskAssembly.GetType('BuildInfo').GetField('Version')
if ($taskField.GetRawConstantValue() -ne $taskVersion) { throw '安装包内嵌版本号不一致。' }
$taskStream = $taskAssembly.GetManifestResourceStream('ff_manifest')
if (-not $taskStream) { throw '安装包缺少文件清单。' }
$taskReader = New-Object System.IO.StreamReader($taskStream)
try { $taskManifest = $taskReader.ReadToEnd() } finally { $taskReader.Dispose() }
$taskExpected = @('FastFile.exe') + @(Get-ChildItem -LiteralPath (Join-Path $taskBuild 'skin') -Recurse -File |
    Where-Object { $_.Name -notlike '*.bak*' -and $_.Name -notlike '*.original*' -and $_.Name -notlike '*.tmp' } |
    ForEach-Object { $_.FullName.Substring($taskBuild.Length + 1).Replace('\', '/') })
$taskSeen = New-Object 'System.Collections.Generic.HashSet[string]'
$taskHash = [System.Security.Cryptography.SHA256]::Create()
try {
    foreach ($taskLine in ($taskManifest -split '\r?\n')) {
        if (-not $taskLine.Trim()) { continue }
        $taskParts = $taskLine.Split('|')
        if ($taskParts.Length -ne 2 -or -not $taskSeen.Add($taskParts[1])) { throw '安装包文件清单无效或重复。' }
        if ($taskExpected -cnotcontains $taskParts[1]) { throw "安装包包含意外文件：$($taskParts[1])" }
        $taskPayload = $taskAssembly.GetManifestResourceStream($taskParts[0])
        if (-not $taskPayload) { throw "安装包缺少资源：$($taskParts[0])" }
        try { $taskEmbeddedHash = [BitConverter]::ToString($taskHash.ComputeHash($taskPayload)).Replace('-', '') }
        finally { $taskPayload.Dispose() }
        $taskSource = Join-Path $taskBuild ($taskParts[1].Replace('/', '\'))
        if ($taskEmbeddedHash -ne (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash) {
            throw "安装包文件与候选构建不一致：$($taskParts[1])"
        }
        Write-Output "Verified $($taskParts[1]): $taskEmbeddedHash"
    }
    if ($taskSeen.Count -ne $taskExpected.Count) { throw '安装包遗漏程序或皮肤文件。' }
    if ($taskAssembly.GetManifestResourceNames().Length -ne $taskSeen.Count + 1) { throw '安装包包含清单之外的资源。' }
} finally { $taskHash.Dispose() }
Write-Output "Installer version $taskVersion and all $($taskSeen.Count) embedded files verified."
