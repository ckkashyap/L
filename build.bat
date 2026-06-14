@echo off
REM build.bat -- Build the L interpreter on Windows
REM
REM Usage: build.bat [NO_TINYGL] [NO_LIBBF]
REM
REM Optional features:
REM   build.bat              -- native Win32/GDI/WASAPI + TinyGL (if built) + libbf (if built)
REM   build.bat NO_TINYGL    -- native Win32/GDI/WASAPI, no TinyGL
REM   build.bat NO_LIBBF     -- built-in bignum only

setlocal

REM --- Defaults ---
set USE_TINYGL=1
set USE_LIBBF=1

REM --- Parse arguments ---
for %%A in (%*) do (
    if /i "%%A"=="NO_TINYGL" set USE_TINYGL=0
    if /i "%%A"=="NO_LIBBF"  set USE_LIBBF=0
)

REM --- Find Visual Studio ---
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install VS 2022 or VS Build Tools 2022+.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS_PATH=%%i
if "%VS_PATH%"=="" (
    echo ERROR: No VS installation with C++ tools found.
    exit /b 1
)
set VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat
set CMAKE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
call "%VCVARS%" >nul 2>&1

cd /d %~dp0
if not exist build mkdir build

REM --- Build TinyGL if needed ---
if "%USE_TINYGL%"=="1" (
    if not exist deps\tinygl\tinygl.lib (
        echo --- Building TinyGL ---
        if not exist "%CMAKE%" (
            echo ERROR: CMake not found at %CMAKE%
            echo Install the CMake component via VS Installer or set USE_TINYGL=0.
            exit /b 1
        )
        "%CMAKE%" -S deps\tinygl -B deps\tinygl\build_msvc ^
            -G "NMake Makefiles" ^
            -DCMAKE_BUILD_TYPE=Release ^
            -DTINYGL_BUILD_SHARED=OFF ^
            -DTINYGL_BUILD_EXAMPLES=OFF ^
            -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON ^
            -DCMAKE_C_FLAGS="/nologo /O2 /W0"
        if errorlevel 1 goto fail
        "%CMAKE%" --build deps\tinygl\build_msvc
        if errorlevel 1 goto fail
        if exist deps\tinygl\build_msvc\src\tinygl-static.lib (
            copy /Y deps\tinygl\build_msvc\src\tinygl-static.lib deps\tinygl\tinygl.lib >nul
            echo TinyGL OK
        ) else (
            echo ERROR: tinygl-static.lib not found after build.
            dir deps\tinygl\build_msvc\src\ 2>nul
            goto fail
        )
    ) else (
        echo TinyGL already built, skipping.
    )
)

REM --- Build libbf if needed ---
if "%USE_LIBBF%"=="1" (
    if not exist deps\libbf\libbf.lib (
        if exist deps\libbf\libbf.c (
            echo --- Building libbf ---
            pushd deps\libbf
            cl.exe /O2 /W0 /nologo /c libbf.c cutils.c >nul 2>&1
            if errorlevel 1 (
                echo WARNING: libbf build failed -- bignum will use built-in fallback
            ) else (
                lib.exe /nologo /OUT:libbf.lib libbf.obj cutils.obj >nul 2>&1
                if errorlevel 1 (
                    echo WARNING: libbf.lib creation failed
                ) else (
                    echo libbf OK
                )
            )
            popd
        )
    ) else (
        echo libbf already built, skipping.
    )
)

REM --- Assemble nmake flags ---
set BUILD_FLAGS=
if "%USE_TINYGL%"=="1" set BUILD_FLAGS=%BUILD_FLAGS% HAVE_TINYGL=1
if "%USE_LIBBF%"=="1"  if exist deps\libbf\libbf.lib set BUILD_FLAGS=%BUILD_FLAGS% HAVE_LIBBF=1

echo --- Building picolisp (%BUILD_FLAGS%) ---
nmake /nologo /f Makefile.win %BUILD_FLAGS%
if errorlevel 1 goto fail

REM --- Build term_helper.dll (required for vi editor) ---
cl.exe /LD /O2 src\term_helper.c /Fe:build\term_helper.dll >nul 2>&1
if errorlevel 1 (
    echo WARNING: term_helper.dll build failed - vi editor will not work
) else (
    echo term_helper.dll OK
)

echo.
echo Build OK: build\l.exe
goto end

:fail
echo.
echo BUILD FAILED
exit /b 1

:end
endlocal
