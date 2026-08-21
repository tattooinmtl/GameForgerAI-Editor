@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo Visual Studio Installer's vswhere.exe was not found.
    echo Open Visual Studio 2026 and select "Open a local folder", then choose:
    echo %~dp0
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property productPath`) do (
    set "DEVENV=%%i"
)

if not defined DEVENV (
    echo A Visual Studio installation with Desktop development with C++ was not found.
    echo Open .vsconfig in this folder to install the required components.
    pause
    exit /b 1
)

start "" "%DEVENV%" "%~dp0"
