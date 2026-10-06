param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildVersion=(Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if($buildVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid VERSION' }
$buildTemplate=Get-Content -LiteralPath (Join-Path $projectRoot 'res/FastFileBuildInfo.h.in') -Raw
$buildText=$buildTemplate.Replace('@FASTFILE_VERSION@',$buildVersion).Replace('@FASTFILE_VERSION_COMMAS@',$buildVersion.Replace('.',','))
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$buildHeader=Join-Path $OutputDirectory 'FastFileBuildInfo.h'
if(!(Test-Path -LiteralPath $buildHeader) -or [IO.File]::ReadAllText($buildHeader) -ne $buildText) { [IO.File]::WriteAllText($buildHeader,$buildText,[Text.Encoding]::ASCII) }
