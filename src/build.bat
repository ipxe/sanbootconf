@echo off

if "%ddkdir%" == "" set ddkdir=C:\WinDDK\7600.16385.1
set setenv=%ddkdir%\bin\setenv.bat %ddkdir%

for /f "delims=" %%i in ('cd') do set cwd=%%i

rem Build code
rem
call :build driver chk wxp i386
call :build installer chk wxp i386
call :build driver chk wnet x64
call :build installer chk wnet x64

rem Create catalogue file
rem
set oslist=2000,XP_x86,XP_x64,Server2003_x86,Server2003_x64,Vista_x86,Vista_x64,Server2008_x86,Server2008_x64
cmd /c "%setenv% && inf2cat /driver:%cwd%\..\bin /os:%oslist%" || exit /b 1

rem Sign files
rem
set certname="Fen Systems Ltd."
set xcertfile=%cwd%\certs\mscv_verisign.cer
cmd /c "%setenv% && certmgr -put -s my -c -n %certname% NUL >NUL" && goto havecert
set certname="Test Certificate for sanbootconf"
set certfile=%cwd%\certs\testcer.cer
if exist %certfile% goto havecert
cmd /c "%setenv% && makecert -r -ss my -n CN=%certname% %certfile%" || exit /b 1
echo ***********************************************************************
echo *                                                                     *
echo * Using a self-signed test certificate                                *
echo *                                                                     *
echo * If you are using Windows Vista or newer, you must enable the use of *
echo * self-signed driver certificates by typing                           *
echo *                                                                     *
echo *     bcdedit -set TestSigning Yes                                    *
echo *                                                                     *
echo * before installing this driver.                                      *
echo *                                                                     *
echo ***********************************************************************
:havecert
set timestamp=http://timestamp.verisign.com/scripts/timestamp.dll
set desc="SAN Boot Configuration Driver"
cmd /c "%setenv% && signtool sign -ac %xcertfile% -n %certname% -t %timestamp% -d %desc% -ph %cwd%\..\bin\i386\*.sys %cwd%\..\bin\amd64\*.sys %cwd%\..\bin\*.cat" || exit /b 1
set certparamfile=%cwd%\certs\params.bat
echo set certname=%certname% > %certparamfile%
echo set timestamp=%timestamp% >> %certparamfile%
echo set desc=%desc% >> %certparamfile%

exit /b

:build
set subdir=%cwd%\%1
set ddkenv=%2 %3 %4
echo Building %subdir% for %ddkenv%
cmd /c "%setenv% %ddkenv% && cd /d %subdir% && build /cwg" || exit /b 1
