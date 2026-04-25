# Force a full clean rebuild of the RECVX PC port.
#
# Run from the port/ directory:  .\rebuild.ps1
#
# Preserves build/vcpkg_installed/ (so we don't re-fetch FFmpeg/SDL2, which
# is 5-15 min), but wipes everything CMake and MSBuild might otherwise
# reuse stale.

param(
    [string]$Config = "Debug"
)

$ErrorActionPreference = 'SilentlyContinue'

Write-Host "-- Clearing build artifacts (keeping vcpkg_installed) --"
Remove-Item build\CMakeFiles, build\Debug, build\Release, `
            build\*.vcxproj*, build\*.sln, build\CMakeCache.txt `
            -Recurse -Force

Write-Host "-- Reconfiguring via x64-vcpkg preset --"
cmake --preset x64-vcpkg 2>&1 | Tee-Object configure.txt

Write-Host "-- Building $Config --"
cmake --build build --config $Config 2>&1 | Tee-Object build-errors.txt

$exe = "build\$Config\recvx_pc.exe"
if (Test-Path $exe) {
    Write-Host "-- Build OK: $exe --" -ForegroundColor Green
    Write-Host "-- Run with: .\run.ps1   (mirrors output to runtime.log) --"
} else {
    Write-Host "-- BUILD FAILED: $exe missing. See build-errors.txt --" -ForegroundColor Red
}
