# run_tests.ps1 -- Windows test runner
param(
    [string]$Interp = "build\picolisp.exe"
)

$pass = 0; $fail = 0; $skip = 0

if (-not (Test-Path $Interp)) {
    Write-Error "Interpreter not found: $Interp. Run do_build.bat first."
}

Get-ChildItem "tests\test_*.l" | ForEach-Object {
    $name = $_.Name
    # Skip SDL tests
    if ($name -match "sdl|gfx") {
        Write-Host "SKIP $name (SDL)" -ForegroundColor DarkGray
        $skip++
        return
    }
    try {
        $out = & $Interp $_.FullName 2>&1 | Out-String
        if ($out -match "ALL TESTS PASSED") {
            Write-Host "PASS $name" -ForegroundColor Green
            $pass++
        } else {
            Write-Host "FAIL $name (no PASSED marker)" -ForegroundColor Red
            Write-Host $out
            $fail++
        }
    } catch {
        Write-Host "FAIL $name (exception: $_)" -ForegroundColor Red
        $fail++
    }
}

Write-Host ""
Write-Host "Results: $pass passed, $fail failed, $skip skipped"
if ($fail -gt 0) { exit 1 }
