$projDir = 'C:\s\L'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat'

$bat = [System.IO.Path]::GetTempFileName() + '.bat'
$cmd = "@echo off`r`ncd /d `"$projDir`"`r`ncall `"$vcvars`" >nul 2>&1`r`nnmake -f Makefile.win HAVE_UV=1 clean`r`nif not exist build mkdir build`r`nnmake -f Makefile.win HAVE_UV=1`r`necho EXITCODE=%ERRORLEVEL%"
[System.IO.File]::WriteAllText($bat, $cmd)
$out = cmd /c $bat 2>&1
$out
Remove-Item $bat -ErrorAction SilentlyContinue
