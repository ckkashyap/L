$projDir = 'C:\s\L'
$bat = [System.IO.Path]::GetTempFileName() + '.bat'
$cmd = "@echo off`r`ncd /d `"$projDir`"`r`ncall `"C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1`r`nnmake /nologo -f Makefile.win`r`necho NMAKE_EXIT=%ERRORLEVEL%"
[System.IO.File]::WriteAllText($bat, $cmd)
$out = cmd /c $bat 2>&1
$out
Remove-Item $bat -ErrorAction SilentlyContinue
