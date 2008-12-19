@echo off

if "%ddkdir%" == "" set ddkdir=C:\WinDDK\6001.18001

for /f "delims=" %%i in ('cd') do set cwd=%%i

call :build driver chk w2k i386
call :build installer chk w2k i386
call :build driver chk wnet x64
call :build installer chk wnet x64

goto :end

:build
set subdir=%cwd%\%1
set ddkenv=%2 %3 %4
echo Building %subdir% for %ddkenv%
cmd /c "%ddkdir%\bin\setenv.bat %ddkdir% %ddkenv% && cd /d %subdir% && build /c" || exit /b 1
goto :end

:end
