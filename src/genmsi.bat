@echo off

for /f "delims=" %%i in ('cd') do set cwd=%%i

"%WIX%\bin\candle" -nologo -arch x86 -o %cwd%\msi\sanbootconf.wixobj %cwd%\msi\sanbootconf.wxs
"%WIX%\bin\light" -nologo -o %cwd%\..\sanbootconf.msi -pdbout %cwd%\msi\sanbootconf.wixpdb -b %cwd%\..\bin %cwd%\msi\sanbootconf.wixobj -ext WixUIExtension
