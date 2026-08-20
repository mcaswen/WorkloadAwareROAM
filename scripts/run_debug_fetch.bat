@echo off
call "%~dp0common.bat" debug-fetch %*
exit /b %errorlevel%
