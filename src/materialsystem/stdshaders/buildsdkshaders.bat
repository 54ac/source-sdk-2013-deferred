@echo off
setlocal

set BUILD_SHADER=call buildshaders.bat

if "%SOURCEDIR%"=="" set SOURCEDIR=..\..\
if "%GAMEDIR%"=="" set GAMEDIR=..\..\..\game\mod_hl2

%BUILD_SHADER% deferred_shaders -game %GAMEDIR% -source %SOURCEDIR% -dx9_30 -force30
