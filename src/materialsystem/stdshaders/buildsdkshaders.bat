@echo off
setlocal

set BUILD_SHADER=call buildshaders.bat

if "%SOURCEDIR%"=="" set SOURCEDIR=..\..\
if "%GAMEDIR%"=="" set GAMEDIR=..\..\..\game\mod_hl2mp

%BUILD_SHADER% sdkshaders_dx9_20b -game %GAMEDIR% -source %SOURCEDIR%
%BUILD_SHADER% sdkshaders_dx9_30   -game %GAMEDIR% -source %SOURCEDIR% -dx9_30 -force30
%BUILD_SHADER% deferred_shaders    -game %GAMEDIR% -source %SOURCEDIR% -dx9_30 -force30

pause
