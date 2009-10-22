@echo off

if "%ddkdir%" == "" set ddkdir=C:\WinDDK\6001.18002

for /f "delims=" %%i in ('cd') do set cwd=%%i

set setenv=%ddkdir%\bin\setenv.bat %ddkdir% chk wnet x64
set cert=%cwd%\bin\testcer.cer

if not exist %cert% cmd /c "%setenv% && makecert -r -pe -ss PrivateCertStore -n CN=fensystems.co.uk %cert% && certmgr /add %cert% /s /r localMachine root && certmgr /add %cert% /s /r localMachine trustedpublisher" || exit /b 1
bcdedit -set TestSigning on || exit /b 1
bcdedit -set NoIntegrityChecks on || exit /b 1

cmd /c "%setenv% && signtool sign /s PrivateCertStore /n fensystems.co.uk /t http://timestamp.verisign.com/scripts/timestamp.dll %cwd%\bin\i386\*.sys %cwd%\bin\amd64\*.sys " || exit /b 1
