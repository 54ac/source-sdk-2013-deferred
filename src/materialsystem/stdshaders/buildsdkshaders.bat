@echo off
setlocal

set BUILD_SHADER=call buildshaders.bat

if "%SOURCEDIR%"=="" set SOURCEDIR=..\..\
if "%GAMEDIR%"=="" set GAMEDIR=..\..\..\game\mod_hl2mp



:parse_args
if "%1"=="" goto done_args
if /i "%1"=="-dynamic" set dynamic_shaders=1
if /i "%1"=="-game" (
    set GAMEDIR=%~2
    shift
)
shift
goto parse_args
:done_args

%BUILD_SHADER% deferred_shaders -game %GAMEDIR% -source %SOURCEDIR%

if not "%dynamic_shaders%"=="1" pause
