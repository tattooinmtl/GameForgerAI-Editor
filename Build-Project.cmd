@echo off
setlocal
pushd "%~dp0"

echo Setting up Visual Studio x64 environment...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
	echo Visual Studio Installer's vswhere.exe was not found.
	popd
	exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath`) do (
	set "VSINSTALLPATH=%%i"
)

if not defined VSINSTALLPATH (
	echo A Visual Studio installation with Desktop development with C++ was not found.
	popd
	exit /b 1
)

call "%VSINSTALLPATH%\VC\Auxiliary\Build\vcvars64.bat"

if errorlevel 1 (
	echo Failed to set up Visual Studio x64 environment.
	popd
	exit /b 1
)

echo.
echo Configuring CMake project...
cmake --preset windows-x64

if errorlevel 1 (
	echo CMake configuration failed.
	popd
	exit /b 1
)

echo.
echo Building Editor (Debug)...
cmake --build --preset editor-debug

if errorlevel 1 (
	echo Build failed.
	popd
	exit /b 1
)

echo.
echo Build completed successfully!
echo Executable should be at: out\build\windows-x64\Editor\Debug\GameForgerEditor.exe
popd
pause
