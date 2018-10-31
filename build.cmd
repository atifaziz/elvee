@echo off
setlocal
pushd "%~dp0"
if "%1"=="" set CLARGS=/MD /O1
cl %CLARGS% %* elvee.c
popd
