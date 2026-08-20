@echo off
call "%~dp0common.bat" relwithdebinfo-fetch %*
exit /b %errorlevel%
