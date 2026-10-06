param([Parameter(Mandatory = $true)][string]$InstallerPath)
$ErrorActionPreference = 'Stop'
$taskInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
$taskAssembly = [System.Reflection.Assembly]::LoadFile($taskInstaller)
$taskMethod = $taskAssembly.GetType('Setup').GetMethod('KillRunning',
    [System.Reflection.BindingFlags]'NonPublic,Static')
if (-not $taskMethod) { throw '缺少安装目录进程关闭方法。' }
$taskTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$taskName = 'FastFileInstallerOwnership_' + [guid]::NewGuid().ToString('N')
$taskRoot = [System.IO.Path]::GetFullPath((Join-Path $taskTemp $taskName))
$taskOwned = Join-Path $taskRoot 'owned'
$taskForeign = Join-Path $taskRoot 'foreign'
$taskProcesses = @()
try {
    New-Item -ItemType Directory -Path $taskOwned,$taskForeign -Force | Out-Null
    $taskSource = Join-Path $taskRoot 'fixture.cs'
    [System.IO.File]::WriteAllText($taskSource,
        'class AgentFixture { static void Main() { System.Threading.Thread.Sleep(60000); } }')
    $taskCompiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $taskOwnedExe = Join-Path $taskOwned 'FastFileAgent.exe'
    & $taskCompiler /nologo /target:winexe /platform:anycpu "/out:$taskOwnedExe" $taskSource
    if ($LASTEXITCODE -ne 0) { throw '进程测试夹具编译失败。' }
    $taskForeignExe = Join-Path $taskForeign 'FastFileAgent.exe'
    Copy-Item -LiteralPath $taskOwnedExe -Destination $taskForeignExe
    $taskOwnedProcess = Start-Process -FilePath $taskOwnedExe -PassThru -WindowStyle Hidden
    $taskProcesses += $taskOwnedProcess
    $taskForeignProcess = Start-Process -FilePath $taskForeignExe -PassThru -WindowStyle Hidden
    $taskProcesses += $taskForeignProcess
    Start-Sleep -Milliseconds 500
    $taskArguments = New-Object string[] 1
    $taskArguments[0] = $taskOwned
    $taskMethod.Invoke($null,$taskArguments) | Out-Null
    if (-not $taskOwnedProcess.WaitForExit(3000)) { throw '目标安装目录的代理没有结束。' }
    $taskForeignProcess.Refresh()
    if ($taskForeignProcess.HasExited) { throw '其他目录的同名代理被错误结束。' }
    Write-Output 'Passed: installer ends its own agent and preserves same-name processes in other directories.'
}
finally {
    foreach ($taskProcess in $taskProcesses) {
        try { if (-not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit(3000) | Out-Null } }
        finally { $taskProcess.Dispose() }
    }
    # Verify the computed recursive removal stays within this exclusively created fixture.
    if ([System.IO.Path]::GetFullPath($taskRoot) -ne (Join-Path $taskTemp $taskName) -or
        -not $taskRoot.StartsWith($taskTemp,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw '测试清理路径校验失败。'
    }
    if (Test-Path -LiteralPath $taskRoot) { Remove-Item -LiteralPath $taskRoot -Recurse -Force }
}
