@echo off
call "%~dp0common.bat" debug-fetch --smoke-test %*
exit /b %errorlevel%
