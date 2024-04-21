@echo off

msbuild SKIV.sln -t:Rebuild -p:Configuration=Release -p:Platform=Win32 -m

if %ERRORLEVEL%==0 goto build_success
 
:build_fail
Pause
Exit /b 1 

:build_success
Exit /b 0