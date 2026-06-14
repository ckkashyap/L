param([string]$TestFile = 'C:\s\L\tests\test_new_features.l')
$exe = 'C:\s\L\build\picolisp.exe'
$out = [System.IO.Path]::GetTempFileName()
$err = [System.IO.Path]::GetTempFileName()
$p = Start-Process -FilePath $exe -ArgumentList $TestFile -RedirectStandardOutput $out -RedirectStandardError $err -Wait -PassThru -NoNewWindow
Get-Content $out -ErrorAction SilentlyContinue
Get-Content $err -ErrorAction SilentlyContinue
Write-Host "Exit: $($p.ExitCode)"
Remove-Item $out, $err -ErrorAction SilentlyContinue
