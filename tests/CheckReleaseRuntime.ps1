param([Parameter(Mandatory = $true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$dumpbin = $null
# Use the compiler selected by this CMake build, including hosted VS versions
# whose setup metadata is not yet recognized by the bundled vswhere.
$buildRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($BuildDirectory))
$cmakeFiles = Join-Path $buildRoot 'CMakeFiles'
if (Test-Path -LiteralPath $cmakeFiles) {
    foreach ($info in @(Get-ChildItem -LiteralPath $cmakeFiles -Filter CMakeCXXCompiler.cmake -Recurse -File)) {
        $compiler = [regex]::Match((Get-Content -LiteralPath $info.FullName -Raw), '(?m)^set\(CMAKE_CXX_COMPILER "([^"]+)"\)')
        if ($compiler.Success) {
            $candidate = Join-Path (Split-Path -Parent $compiler.Groups[1].Value) 'dumpbin.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $dumpbin = $candidate; break }
        }
    }
}
if (-not $dumpbin) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere is required to locate the MSVC inspection tool.' }
    $dumpbin = & $vswhere -all -prerelease -products '*' -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/dumpbin.exe' | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or -not $dumpbin) { throw 'MSVC dumpbin was not found.' }
}
Write-Host "Inspecting with $dumpbin"
foreach ($name in @('FastFile.exe', 'FastFileAgent.exe')) {
    $binary = Join-Path $BuildDirectory $name
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing binary: $binary" }
    $imports = & $dumpbin /DEPENDENTS $binary
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect $name" }
    $externalRuntime = $imports | Where-Object { $_ -match '(?i)\b(?:VCRUNTIME\d\S*|MSVCP\d\S*|CONCRT\d\S*|api-ms-win-crt-\S*)\.dll\b' }
    if ($externalRuntime) { throw "$name requires an external C++ runtime: $($externalRuntime -join ', ')" }
    Write-Host "${name}: no external C++ runtime dependency."
}
