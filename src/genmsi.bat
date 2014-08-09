@echo off

if "%ddkdir%" == "" set ddkdir=C:\WinDDK\7600.16385.1
set setenv=%ddkdir%\bin\setenv.bat %ddkdir%

for /f "delims=" %%i in ('cd') do set cwd=%%i

"%WIX%\bin\candle" -nologo -arch x86 -o %cwd%\msi\sanbootconf.wixobj %cwd%\msi\sanbootconf.wxs
"%WIX%\bin\light" -nologo -o %cwd%\..\sanbootconf.msi -pdbout %cwd%\msi\sanbootconf.wixpdb -b %cwd%\..\bin %cwd%\msi\sanbootconf.wixobj -ext WixUIExtension

call %cwd%\certs\params.bat
cmd /c "%setenv% && signtool sign -n %certname% -t %timestamp% -d %desc% %cwd%\..\sanbootconf.msi" || exit /b 1
