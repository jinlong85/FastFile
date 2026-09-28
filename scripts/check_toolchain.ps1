# FastFile — quick toolchain probe
$ErrorActionPreference = "Continue"
Write-Host "=== FastFile toolchain check ==="
foreach ($cmd in @("cmake","git","cl","msbuild")) {
  $w = Get-Command $cmd -ErrorAction SilentlyContinue
  if ($w) { Write-Host ("[OK] {0} -> {1}" -f $cmd, $w.Source) }
  else { Write-Host ("[MISS] {0}" -f $cmd) }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
  Write-Host "[OK] vswhere"
  & $vswhere -latest -products * -property displayName
  & $vswhere -latest -products * -property installationPath
} else {
  Write-Host "[MISS] vswhere / Visual Studio Installer"
}