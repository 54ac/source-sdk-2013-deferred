@echo off
pushd "%~dp0"
call buildsdkshaders.bat -dynamic %*
popd
