# scripts/fetch_libffi_win.ps1
# Downloads and installs prebuilt libffi (MSVC x64 static) into deps\libffi\
# Requires: vcpkg on PATH (https://vcpkg.io) OR manual placement.
#
# Usage:  powershell -File scripts\fetch_libffi_win.ps1
#
# If vcpkg is not available, manually place:
#   deps\libffi\include\ffi.h
#   deps\libffi\include\ffitarget.h
#   deps\libffi\lib\libffi.lib

param([string]$VcpkgRoot = $env:VCPKG_ROOT)

$ErrorActionPreference = 'Stop'

# Try to find vcpkg
if (-not $VcpkgRoot) {
    $candidates = @(
        "C:\vcpkg",
        "C:\src\vcpkg",
        "$env:USERPROFILE\vcpkg",
        "$env:LOCALAPPDATA\vcpkg"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\vcpkg.exe") { $VcpkgRoot = $c; break }
    }
}

if (-not $VcpkgRoot -or -not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Error @"
vcpkg not found. Options:
  1. Install vcpkg: https://vcpkg.io/en/getting-started.html
     Then run: vcpkg install libffi:x64-windows-static
  2. Set VCPKG_ROOT environment variable and re-run this script.
  3. Manually place prebuilt files:
       deps\libffi\include\ffi.h
       deps\libffi\include\ffitarget.h
       deps\libffi\lib\libffi.lib
"@
    exit 1
}

Write-Host "Using vcpkg at: $VcpkgRoot"
& "$VcpkgRoot\vcpkg.exe" install "libffi:x64-windows-static"
if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install failed"; exit 1 }

$installed = "$VcpkgRoot\installed\x64-windows-static"
$dest      = Join-Path $PSScriptRoot "..\deps\libffi"

New-Item -ItemType Directory -Force "$dest\include" | Out-Null
New-Item -ItemType Directory -Force "$dest\lib"     | Out-Null

Copy-Item "$installed\include\ffi.h"         "$dest\include\ffi.h"         -Force
Copy-Item "$installed\include\ffitarget.h"   "$dest\include\ffitarget.h"   -Force
Copy-Item "$installed\lib\libffi.lib"        "$dest\lib\libffi.lib"        -Force

Write-Host "libffi installed to deps\libffi\" -ForegroundColor Green
Write-Host "Now rebuild with: nmake -f Makefile.win"
