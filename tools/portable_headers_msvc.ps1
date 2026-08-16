param(
  [Parameter(Mandatory = $true)]
  [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
$archive = "build/portable-assets/tess-$ExpectedVersion-headers.zip"
$archiveName = Split-Path $archive -Leaf
$sums = 'build/portable-assets/SHA256SUMS'
$escapedArchiveName = [regex]::Escape($archiveName)
$checksumLines = @(
  Get-Content -LiteralPath $sums | Where-Object {
    $_ -match "^([0-9a-f]{64})  $escapedArchiveName$"
  }
)
if ($checksumLines.Count -ne 1) {
  throw "SHA256SUMS has no unique entry for $archiveName"
}
$expectedHash = $checksumLines[0].Substring(0, 64)
$actualHash = (
  Get-FileHash -LiteralPath $archive -Algorithm SHA256
).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
  throw "SHA-256 mismatch for $archiveName"
}
Expand-Archive -LiteralPath $archive -DestinationPath build/portable-zip

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$install = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $install) {
  throw 'Visual Studio C++ tools were not found'
}

$module = Join-Path $install 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $module
Enter-VsDevShell -VsInstallPath $install `
  -SkipAutomaticLocation -DevCmdArguments '-arch=x64'

$include = "build/portable-zip/tess-$ExpectedVersion/include"
cl /nologo /std:c++20 /EHsc /GR- "/I$include" `
  tests/portable_headers_consumer.cc /Fe:build/portable-msvc-consumer.exe
if ($LASTEXITCODE -ne 0) {
  throw 'direct MSVC portable-header compile failed'
}
& build/portable-msvc-consumer.exe
if ($LASTEXITCODE -ne 0) {
  throw 'direct MSVC portable-header consumer failed'
}
