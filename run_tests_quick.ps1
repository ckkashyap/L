$exe = 'C:\s\L\build\picolisp.exe'
$tests = @(
    'test_gc.l', 'test_guard.l', 'test_io.l', 'test_letrec.l',
    'test_macro_only.l', 'test_make.l', 'test_math.l', 'test_new_features.l',
    'test_perf2.l', 'test_perf3.l', 'test_prims_coverage.l', 'test_quasiquote.l',
    'test_string.l', 'test_vec.l', 'test_bignum.l', 'test_char.l',
    'test_core.l', 'test_do.l', 'test_ds.l'
)

foreach ($t in $tests) {
    $f = "C:\s\L\tests\$t"
    $out = [System.IO.Path]::GetTempFileName()
    $err = [System.IO.Path]::GetTempFileName()
    $job = Start-Job -ScriptBlock {
        param($exe, $f, $out, $err)
        $p = Start-Process $exe -ArgumentList $f -RedirectStandardOutput $out -RedirectStandardError $err -Wait -PassThru -NoNewWindow -WorkingDirectory 'C:\s\L'
        $p.ExitCode
    } -ArgumentList $exe, $f, $out, $err
    $done = Wait-Job $job -Timeout 10
    if ($done) {
        $outTxt = (Get-Content $out -ea 0) -join "`n"
        $errTxt = (Get-Content $err -ea 0) -join "`n"
        if ($outTxt -match 'ALL TESTS PASSED') {
            Write-Host "PASS $t" -ForegroundColor Green
        } else {
            $preview = if ($errTxt) { $errTxt.Split("`n")[0] } else { $outTxt.Split("`n") | Select-Object -Last 2 | Select-Object -First 1 }
            Write-Host "FAIL $t  [$preview]" -ForegroundColor Red
        }
    } else {
        Stop-Job $job
        Write-Host "TIMEOUT $t" -ForegroundColor Yellow
    }
    Remove-Job $job -ea 0
    Remove-Item $out, $err -ea 0
}
