# tools/rebuild_ttf.ps1 -- recompile ttf.c and relink l.exe
$VS_ROOT  = "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
$MSVC_VER = "14.50.35717"
$CL       = "$VS_ROOT\VC\Tools\MSVC\$MSVC_VER\bin\Hostx64\x64\cl.exe"
$LINK     = "$VS_ROOT\VC\Tools\MSVC\$MSVC_VER\bin\Hostx64\x64\link.exe"
$WK_ROOT  = "C:\Program Files (x86)\Windows Kits\10"
$WK_VER   = "10.0.26100.0"
$env:INCLUDE = "$VS_ROOT\VC\Tools\MSVC\$MSVC_VER\include;$WK_ROOT\Include\$WK_VER\um;$WK_ROOT\Include\$WK_VER\ucrt;$WK_ROOT\Include\$WK_VER\shared"
$env:LIB    = "$VS_ROOT\VC\Tools\MSVC\$MSVC_VER\lib\x64;$WK_ROOT\Lib\$WK_VER\um\x64;$WK_ROOT\Lib\$WK_VER\ucrt\x64"
Set-Location C:\s\L

$haveLibbf  = Test-Path "deps\libbf\libbf.lib"
$haveTinygl = Test-Path "deps\tinygl\tinygl.lib"

$CFLAGS = [System.Collections.Generic.List[string]]@(
    "/std:c17","/O2","/W3","/FImsvc_compat.h",
    "/Isrc","/Ideps\libbf",
    "/DHAVE_FFI=1","/Ideps\libffi\include"
)
if ($haveLibbf)  { $CFLAGS.Add("/DHAVE_LIBBF=1") }
if ($haveTinygl) { $CFLAGS.Add("/DHAVE_TINYGL=1"); $CFLAGS.Add("/Ideps\tinygl\include") }

function Compile($src, $obj) {
    Write-Host "Compiling $src ..."
    & $CL @CFLAGS /nologo /Fo$obj /c $src 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $src"; exit 1 }
}

# Recompile changed files
# All source files needed for l.exe
$ALL_SRCS = @(
    "src\main.c","src\heap.c","src\sym.c","src\reader.c","src\eval.c",
    "src\print.c","src\prims.c","src\bignum.c",
    "src\native_io_win32.c","src\native_gfx_win32.c","src\pipe_win32.c",
    "src\coro.c","src\ttf.c","src\ffi.c","src\callbacks.c"
)
if ($haveTinygl) { $ALL_SRCS += "src\tinygl_bridge.c" }

foreach ($src in $ALL_SRCS) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = "build\$base.obj"
    if (-not (Test-Path $obj) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $obj).LastWriteTime)) {
        Compile $src $obj
    }
}

Write-Host "--- Linking l.exe ---"
$OBJS = @(
    "build\main.obj","build\heap.obj","build\sym.obj","build\reader.obj",
    "build\eval.obj","build\print.obj","build\prims.obj","build\bignum.obj",
    "build\native_io_win32.obj","build\native_gfx_win32.obj",
    "build\pipe_win32.obj","build\coro.obj","build\ttf.obj",
    "build\ffi.obj","build\callbacks.obj"
)
if ($haveTinygl -and (Test-Path "build\tinygl_bridge.obj")) { $OBJS += "build\tinygl_bridge.obj" }

$LIBS = @("ws2_32.lib","iphlpapi.lib","userenv.lib","psapi.lib",
          "gdi32.lib","user32.lib","ole32.lib","ksuser.lib","winmm.lib",
          "deps\libffi\lib\libffi.lib")
if ($haveLibbf)  { $LIBS += "deps\libbf\libbf.lib" }
if ($haveTinygl -and (Test-Path "build\tinygl_bridge.obj")) { $LIBS += "deps\tinygl\tinygl.lib" }

& $LINK /OUT:build\l.exe /nologo /ignore:4098 /ignore:4217 /ignore:4286 @OBJS @LIBS 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "--- Build OK: build\l.exe ---"
} else {
    Write-Host "LINK FAILED"
    exit 1
}
