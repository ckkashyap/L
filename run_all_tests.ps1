$exe = 'C:\s\L\build\l.exe'
$pass = 0; $fail = 0; $skip = 0

Get-ChildItem 'C:\s\L\tests\test_*.l' | ForEach-Object {
    $name = $_.Name
    if ($name -match 'sdl|gfx|http|pmap|uv') {
        Write-Host "SKIP $name" -ForegroundColor DarkGray
        $skip++
        return
    }
    $out = [System.IO.Path]::GetTempFileName()
    $err = [System.IO.Path]::GetTempFileName()
    $p = Start-Process -FilePath $exe -ArgumentList $_.FullName -RedirectStandardOutput $out -RedirectStandardError $err -Wait -PassThru -NoNewWindow -WorkingDirectory 'C:\s\L'
    $outTxt = (Get-Content $out -ErrorAction SilentlyContinue) -join "`n"
    $errTxt = (Get-Content $err -ErrorAction SilentlyContinue) -join "`n"
    Remove-Item $out, $err -ErrorAction SilentlyContinue
    if ($outTxt -match 'ALL TESTS PASSED') {
        Write-Host "PASS $name" -ForegroundColor Green
        $pass++
    } else {
        Write-Host "FAIL $name" -ForegroundColor Red
        if ($errTxt) { Write-Host "  ERR: $errTxt" -ForegroundColor Yellow }
        $fail++
    }
}

Write-Host ""
Write-Host "Results: $pass passed, $fail failed, $skip skipped"
