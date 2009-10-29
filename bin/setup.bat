@echo off

title SAN Boot Configuration Driver

set cpu=
if "%PROCESSOR_ARCHITECTURE%"=="x86" set cpu=i386
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set cpu=amd64
if "%PROCESSOR_ARCHITEW6432%"=="AMD64" set cpu=amd64
if "%cpu%"=="" goto cpuerror

%0\..\%cpu%\setup.exe || exit /B 1
exit /B

:cpuerror
echo Could not determine CPU type
pause
exit /B 1
