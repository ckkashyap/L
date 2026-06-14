# setup.ps1 -- First-time dependency setup for Windows
# Run as: powershell -ExecutionPolicy Bypass -File setup.ps1
#
# Dependencies built/downloaded:
#   libbf     -- arbitrary precision bignum
#   libffi    -- foreign function interface (bundled in deps\libffi\)
#   TinyGL    -- software OpenGL rasterizer (optional, for 3D demos)
#   JetBrains Mono -- TTF font for graphical vi mode

param(
    [switch]$SkipDownload,
    [switch]$Max
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir
[System.Environment]::CurrentDirectory = $ScriptDir

Write-Host "=== L Interpreter Windows Setup ===" -ForegroundColor Cyan

# Create directories
$dirs = @("deps\libbf", "deps\fonts", "deps\tinygl", "build",
          "deps\samples\piano", "deps\samples\drums", "temp")
foreach ($d in $dirs) {
    if (-not (Test-Path (Join-Path $ScriptDir $d))) {
        New-Item -ItemType Directory -Path (Join-Path $ScriptDir $d) -Force | Out-Null
    }
}

# Find VS Build Tools
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Please install VS 2022 or VS Build Tools 2022+."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "No VS installation with C++ tools found."
}

Write-Host "Found VS at: $vsPath" -ForegroundColor Green

# ---------------------------------------------------------------------------
# libbf -- arbitrary precision bignum library
# ---------------------------------------------------------------------------
Write-Host "--- Building libbf ---" -ForegroundColor Yellow
$libbfC   = Join-Path $ScriptDir "deps\libbf\libbf.c"
$libbfLib = Join-Path $ScriptDir "deps\libbf\libbf.lib"
if (-not (Test-Path $libbfLib)) {
    if (-not (Test-Path $libbfC)) {
        if (-not $SkipDownload) {
            Write-Host "Downloading libbf from bellard.org..." -ForegroundColor Yellow
            $libbfUrl = "https://bellard.org/libbf/libbf-2025-06-03.tar.gz"
            $libbfTmp = Join-Path $ScriptDir "temp\libbf.tar.gz"
            try {
                Invoke-WebRequest -Uri $libbfUrl -OutFile $libbfTmp -UseBasicParsing -TimeoutSec 60
                $libbfDir = Join-Path $ScriptDir "deps\libbf"
                & tar -xzf $libbfTmp --strip-components=1 -C $libbfDir
                Remove-Item $libbfTmp -ErrorAction SilentlyContinue
                # Replace cutils.h and cutils.c with MSVC-compatible versions
                if (Test-Path (Join-Path $ScriptDir "compat\libbf_cutils.h")) {
                    Copy-Item (Join-Path $ScriptDir "compat\libbf_cutils.h") `
                              (Join-Path $libbfDir "cutils.h") -Force
                    Copy-Item (Join-Path $ScriptDir "compat\libbf_cutils.c") `
                              (Join-Path $libbfDir "cutils.c") -Force
                }
                Write-Host "libbf sources downloaded." -ForegroundColor Green
            } catch {
                Write-Warning "Could not download libbf: $_"
                Write-Warning "Place libbf sources in deps\libbf\ manually from https://bellard.org/libbf/"
            }
        }
    }
    if (Test-Path $libbfC) {
        $vcvars   = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
        $buildCmd = "`"$vcvars`" && cd /d `"$(Join-Path $ScriptDir 'deps\libbf')`" && cl.exe /std:c17 /O2 /nologo /c libbf.c /Folibbf.obj && cl.exe /std:c17 /O2 /nologo /c cutils.c /Focutils.obj && lib /nologo /OUT:libbf.lib libbf.obj cutils.obj"
        cmd /c $buildCmd
        if (Test-Path $libbfLib) {
            Write-Host "libbf built OK" -ForegroundColor Green
        } else {
            Write-Warning "libbf build failed - bignum will use built-in fallback."
        }
    }
} else {
    Write-Host "libbf already built, skipping." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# TinyGL -- software OpenGL rasterizer (optional, for 3D demos)
# ---------------------------------------------------------------------------
if (-not $SkipDownload) {
    $tinyglCmake = Join-Path $ScriptDir "deps\tinygl\CMakeLists.txt"
    if (-not (Test-Path $tinyglCmake)) {
        Write-Host "--- Downloading TinyGL (C-Chads fork) ---" -ForegroundColor Yellow
        $tinyglUrl = "https://github.com/C-Chads/tinygl/archive/refs/heads/main.tar.gz"
        $tinyglTmp = Join-Path $ScriptDir "temp\tinygl.tar.gz"
        $tinyglDir = Join-Path $ScriptDir "deps\tinygl"
        try {
            Invoke-WebRequest -Uri $tinyglUrl -OutFile $tinyglTmp -UseBasicParsing -TimeoutSec 60
            & tar -xzf $tinyglTmp --strip-components=1 -C $tinyglDir
            Remove-Item $tinyglTmp -ErrorAction SilentlyContinue
            Write-Host "TinyGL sources downloaded." -ForegroundColor Green
        } catch {
            Write-Warning "Could not download TinyGL: $_"
            Write-Warning "TinyGL support will not be available (build.bat NO_TINYGL to skip)."
        }
    } else {
        Write-Host "TinyGL sources already present." -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
# JetBrainsMono font -- for vi --gfx TTF rendering
# ---------------------------------------------------------------------------
if (-not $SkipDownload) {
    $fontFile = Join-Path $ScriptDir "deps\fonts\JetBrainsMono-Regular.ttf"
    if (-not (Test-Path $fontFile)) {
        Write-Host "--- Downloading JetBrainsMono font ---" -ForegroundColor Yellow
        try {
            $jbmVer = "2.304"
            $jbmUrl = "https://github.com/JetBrains/JetBrainsMono/releases/download/v$jbmVer/JetBrainsMono-$jbmVer.zip"
            $jbmTmp = Join-Path $ScriptDir "temp\JetBrainsMono.zip"
            Invoke-WebRequest -Uri $jbmUrl -OutFile $jbmTmp -UseBasicParsing -TimeoutSec 60
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip = [System.IO.Compression.ZipFile]::OpenRead($jbmTmp)
            $entry = $zip.Entries | Where-Object { $_.FullName -like "*/JetBrainsMono-Regular.ttf" } | Select-Object -First 1
            if ($entry) {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $fontFile, $true)
                Write-Host "JetBrainsMono-Regular.ttf downloaded." -ForegroundColor Green
            } else {
                Write-Warning "JetBrainsMono-Regular.ttf not found in zip."
            }
            $zip.Dispose()
            Remove-Item $jbmTmp -ErrorAction SilentlyContinue
        } catch {
            Write-Warning "Could not download JetBrainsMono font: $_"
            Write-Warning "Place JetBrainsMono-Regular.ttf in deps\fonts\ manually."
        }
    } else {
        Write-Host "JetBrainsMono font already present." -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
# Generate audio samples (WAV files) for live-coding demos
# ---------------------------------------------------------------------------
Write-Host "--- Generating audio samples ---" -ForegroundColor Yellow

function New-SineWav {
    param([string]$Path, [double]$Freq, [int]$DurMs = 2000, [double]$Decay = 3.5)
    $sr      = 44100
    $n       = [int]($sr * $DurMs / 1000)
    $dbytes  = $n * 2
    $ms  = New-Object System.IO.MemoryStream
    $bw  = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([byte[]][char[]]"RIFF")
    $bw.Write([int32](36 + $dbytes))
    $bw.Write([byte[]][char[]]"WAVE")
    $bw.Write([byte[]][char[]]"fmt ")
    $bw.Write([int32]16); $bw.Write([int16]1); $bw.Write([int16]1)
    $bw.Write([int32]$sr); $bw.Write([int32]($sr * 2))
    $bw.Write([int16]2); $bw.Write([int16]16)
    $bw.Write([byte[]][char[]]"data"); $bw.Write([int32]$dbytes)
    for ($i = 0; $i -lt $n; $i++) {
        $t   = $i / $sr
        $env = [Math]::Exp(-$Decay * $t)
        $s   = [Math]::Sin(2.0 * [Math]::PI * $Freq * $t) * $env * 0.8
        $bw.Write([int16]([Math]::Max(-32767,[Math]::Min(32767,[Math]::Round($s * 32767)))))
    }
    $bw.Close()
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

function New-RawWav {
    param([string]$Path, [float[]]$Samples, [int]$SampleRate = 44100)
    $n      = $Samples.Length
    $dbytes = $n * 2
    $ms  = New-Object System.IO.MemoryStream
    $bw  = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([byte[]][char[]]"RIFF"); $bw.Write([int32](36 + $dbytes))
    $bw.Write([byte[]][char[]]"WAVE"); $bw.Write([byte[]][char[]]"fmt ")
    $bw.Write([int32]16); $bw.Write([int16]1); $bw.Write([int16]1)
    $bw.Write([int32]$SampleRate); $bw.Write([int32]($SampleRate * 2))
    $bw.Write([int16]2); $bw.Write([int16]16)
    $bw.Write([byte[]][char[]]"data"); $bw.Write([int32]$dbytes)
    foreach ($s in $Samples) {
        $bw.Write([int16]([Math]::Max(-32767,[Math]::Min(32767,[Math]::Round($s * 32767)))))
    }
    $bw.Close()
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

# Piano notes
$pianoNotes = [ordered]@{
    "C3"  = 130.81; "Cs3" = 138.59; "D3"  = 146.83; "Ds3" = 155.56
    "E3"  = 164.81; "F3"  = 174.61; "Fs3" = 185.00; "G3"  = 196.00
    "Gs3" = 207.65; "A3"  = 220.00; "As3" = 233.08; "B3"  = 246.94
    "C4"  = 261.63; "Cs4" = 277.18; "D4"  = 293.66; "Ds4" = 311.13
    "E4"  = 329.63; "F4"  = 349.23; "Fs4" = 369.99; "G4"  = 392.00
    "Gs4" = 415.30; "A4"  = 440.00; "As4" = 466.16; "B4"  = 493.88
    "C5"  = 523.25
}
$pianoDir = Join-Path $ScriptDir "deps\samples\piano"
foreach ($note in $pianoNotes.GetEnumerator()) {
    $p = Join-Path $pianoDir "$($note.Key).wav"
    if (-not (Test-Path $p)) { New-SineWav -Path $p -Freq $note.Value -DurMs 2500 -Decay 2.5 }
}
Write-Host "Piano samples: $($pianoNotes.Count) notes." -ForegroundColor Green

# Drum samples
$sr = 44100
function New-KickWav([string]$Path) {
    $n = [int]($sr * 0.22)
    $s = New-Object float[] $n
    $phase = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $t    = $i / $sr
        $freq = 45.0 + 110.0 * [Math]::Exp(-$t * 28.0)
        $amp  = 0.85 * [Math]::Exp(-$t * 7.0)
        $s[$i] = [float]([Math]::Sin($phase * 2.0 * [Math]::PI) * $amp)
        $phase += $freq / $sr
    }
    New-RawWav -Path $Path -Samples $s
}
function New-SnareWav([string]$Path) {
    $n    = [int]($sr * 0.18)
    $s    = New-Object float[] $n
    $rand = New-Object System.Random(12345)
    for ($i = 0; $i -lt $n; $i++) {
        $t     = $i / $sr
        $noise = ($rand.NextDouble() * 2.0 - 1.0) * 0.55 * [Math]::Exp(-$t * 20.0)
        $body  = [Math]::Sin($i * 2.0 * [Math]::PI * 200.0 / $sr) * 0.30 * [Math]::Exp(-$t * 32.0)
        $v = [Math]::Max(-0.95, [Math]::Min(0.95, $noise + $body))
        $s[$i] = [float]$v
    }
    New-RawWav -Path $Path -Samples $s
}
function New-HihatWav([string]$Path, [int]$DurMs = 45) {
    $n    = [int]($sr * $DurMs / 1000)
    $s    = New-Object float[] $n
    $rand = New-Object System.Random(67890)
    $decay = if ($DurMs -lt 100) { 90.0 } else { 10.0 }
    for ($i = 0; $i -lt $n; $i++) {
        $t   = $i / $sr
        $v   = ($rand.NextDouble() * 2.0 - 1.0) * 0.35 * [Math]::Exp(-$t * $decay)
        $s[$i] = [float]$v
    }
    New-RawWav -Path $Path -Samples $s
}

$drumsDir = Join-Path $ScriptDir "deps\samples\drums"
if (-not (Test-Path (Join-Path $drumsDir "kick.wav")))  { New-KickWav  (Join-Path $drumsDir "kick.wav") }
if (-not (Test-Path (Join-Path $drumsDir "snare.wav"))) { New-SnareWav (Join-Path $drumsDir "snare.wav") }
if (-not (Test-Path (Join-Path $drumsDir "hihat.wav"))) { New-HihatWav (Join-Path $drumsDir "hihat.wav") -DurMs 45 }
if (-not (Test-Path (Join-Path $drumsDir "ohat.wav")))  { New-HihatWav (Join-Path $drumsDir "ohat.wav")  -DurMs 280 }
Write-Host "Drum samples: kick, snare, hihat, ohat." -ForegroundColor Green

# ---------------------------------------------------------------------------
# Salamander Grand Piano -- real piano samples (cached in temp/)
# ---------------------------------------------------------------------------
if ($Max -and -not $SkipDownload) {
    $pianoRealDir = Join-Path $ScriptDir "deps\samples\piano-real"
    if (-not (Test-Path (Join-Path $pianoRealDir "C4.wav"))) {
        Write-Host "--- Salamander Grand Piano ---" -ForegroundColor Yellow
        New-Item -ItemType Directory -Path $pianoRealDir -Force | Out-Null
        $salamCache = Join-Path $ScriptDir "temp\salamander_piano_v3.tar.xz"
        $salamUrl = "https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPianoV3+20161209_48khz24bit.tar.xz"
        if (-not (Test-Path $salamCache)) {
            Write-Host "  Downloading (~1.2GB, cached in $salamCache for future runs)..." -ForegroundColor Yellow
            try {
                Invoke-WebRequest -Uri $salamUrl -OutFile $salamCache -UseBasicParsing -TimeoutSec 600
            } catch {
                Write-Warning "Download failed: $_"
                Remove-Item $salamCache -ErrorAction SilentlyContinue
            }
        } else {
            Write-Host "  Using cached archive: $salamCache" -ForegroundColor Green
        }
        if (Test-Path $salamCache) {
            Write-Host "  Extracting mezzo-forte layer..." -ForegroundColor Yellow
            & tar -xJf $salamCache --strip-components=2 -C $pianoRealDir --wildcards "*/48khz24bit/*v8.wav" 2>$null
            if ($LASTEXITCODE -ne 0) {
                & tar -xJf $salamCache --strip-components=2 -C $pianoRealDir 2>$null
            }
            # Keep ONLY v8 layer
            Get-ChildItem $pianoRealDir -Filter "*.wav" | Where-Object { $_.Name -notmatch 'v8' } |
                Remove-Item -Force -ErrorAction SilentlyContinue
            Get-ChildItem $pianoRealDir -Filter "*v8.wav" | ForEach-Object {
                Rename-Item $_.FullName ($_.Name -replace 'v8\.wav$', '.wav') -ErrorAction SilentlyContinue
            }
            Get-ChildItem $pianoRealDir -File | Where-Object { $_.Extension -ne '.wav' } |
                Remove-Item -Force -ErrorAction SilentlyContinue
            $count = (Get-ChildItem $pianoRealDir -Filter "*.wav").Count
            Write-Host "  Salamander Piano: $count samples" -ForegroundColor Green
        }
    } else {
        Write-Host "Salamander Piano already present." -ForegroundColor Green
    }
} elseif (-not $Max) {
    Write-Host "Skipping Salamander Piano (pass -Max to download)." -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
# 808 drum kit (synthesized, higher quality than basic)
# ---------------------------------------------------------------------------
$drums808Dir = Join-Path $ScriptDir "deps\samples\drums-808"
if (-not (Test-Path (Join-Path $drums808Dir "kick.wav"))) {
    Write-Host "--- Generating 808 drum kit ---" -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $drums808Dir -Force | Out-Null

    function New-Kick808([string]$Path) {
        $n = [int]($sr * 0.5); $s = New-Object float[] $n; $phase = 0.0
        for ($i = 0; $i -lt $n; $i++) {
            $t = $i / $sr; $freq = 40 + 160 * [Math]::Exp(-$t * 20)
            $amp = 0.95 * [Math]::Exp(-$t * 4)
            $v = [Math]::Sin($phase * 2 * [Math]::PI) * $amp * 1.5
            $s[$i] = [float][Math]::Max(-0.95, [Math]::Min(0.95, $v))
            $phase += $freq / $sr
        }
        New-RawWav -Path $Path -Samples $s
    }
    function New-Snare808([string]$Path) {
        $rand = New-Object System.Random(42); $n = [int]($sr * 0.3)
        $s = New-Object float[] $n
        for ($i = 0; $i -lt $n; $i++) {
            $t = $i / $sr
            $body = [Math]::Sin($i * 2 * [Math]::PI * 180 / $sr) * 0.5 * [Math]::Exp(-$t * 25)
            $noise = ($rand.NextDouble() * 2 - 1) * 0.7 * [Math]::Exp(-$t * 15)
            $s[$i] = [float][Math]::Max(-0.95, [Math]::Min(0.95, $body + $noise))
        }
        New-RawWav -Path $Path -Samples $s
    }
    function New-Clap808([string]$Path) {
        $rand = New-Object System.Random(77); $n = [int]($sr * 0.25)
        $s = New-Object float[] $n
        for ($burst = 0; $burst -lt 4; $burst++) {
            $start = [int]($burst * $sr * 0.012)
            for ($i = 0; $i -lt [Math]::Min([int]($sr * 0.02), $n - $start); $i++) {
                $t = $i / $sr
                $s[$start + $i] += ($rand.NextDouble() * 2 - 1) * 0.4 * [Math]::Exp(-$t * 50)
            }
        }
        for ($i = 0; $i -lt $n; $i++) {
            $t = $i / $sr
            $s[$i] += ($rand.NextDouble() * 2 - 1) * 0.3 * [Math]::Exp(-$t * 12)
            $s[$i] = [float][Math]::Max(-0.95, [Math]::Min(0.95, $s[$i]))
        }
        New-RawWav -Path $Path -Samples $s
    }
    function New-Tom808([string]$Path, [double]$Freq = 80) {
        $n = [int]($sr * 0.4); $s = New-Object float[] $n; $phase = 0.0
        for ($i = 0; $i -lt $n; $i++) {
            $t = $i / $sr; $f = $Freq + 40 * [Math]::Exp(-$t * 15)
            $s[$i] = [float]([Math]::Sin($phase * 2 * [Math]::PI) * 0.8 * [Math]::Exp(-$t * 6))
            $phase += $f / $sr
        }
        New-RawWav -Path $Path -Samples $s
    }

    New-Kick808  (Join-Path $drums808Dir "kick.wav")
    New-Snare808 (Join-Path $drums808Dir "snare.wav")
    New-HihatWav (Join-Path $drums808Dir "hihat.wav") -DurMs 60
    New-HihatWav (Join-Path $drums808Dir "ohat.wav")  -DurMs 400
    New-Clap808  (Join-Path $drums808Dir "clap.wav")
    New-Tom808   (Join-Path $drums808Dir "tom-lo.wav") -Freq 70
    New-Tom808   (Join-Path $drums808Dir "tom-hi.wav") -Freq 120
    Write-Host "808 drums: kick, snare, hihat, ohat, clap, tom-lo, tom-hi." -ForegroundColor Green
} else {
    Write-Host "808 drums already present." -ForegroundColor Green
}

Write-Host ""
Write-Host "Setup complete. Run: build.bat" -ForegroundColor Green
Write-Host "  Options: build.bat NO_TINYGL  /  build.bat NO_LIBBF" -ForegroundColor DarkGray
