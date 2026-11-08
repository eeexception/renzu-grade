# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [switch]$RunTests,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT') { throw 'This script requires Windows x64.' }
if ($Package -and $Configuration -ne 'Release') { throw 'Distribution packages require Release.' }
$renzuRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $renzuRoot 'build-windows' }
foreach ($tool in @('cmake', 'ctest', 'cpack')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing prerequisite: $tool" }
}
& cmake -S $renzuRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -DRENZU_ENABLE_METAL=OFF -DRENZU_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($RunTests -or $Package) {
    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; no package will be created.' }
}
if ($Package) {
    & cpack --config (Join-Path $BuildDirectory 'CPackConfig.cmake') -C $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
}
