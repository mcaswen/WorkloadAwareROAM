@echo off
call "%~dp0common.bat" release-fetch %*
exit /b %errorlevel%
