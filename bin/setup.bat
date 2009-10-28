@echo off

set cpu=
if "%PROCESSOR_ARCHITECTURE%"=="x86" set cpu=i386
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" set cpu=amd64
if "%cpu%"=="" goto cpuerror

%0\..\%cpu%\setup.exe
goto :end

:cpuerror
echo Could not determine CPU type
pause
goto :end

:end
