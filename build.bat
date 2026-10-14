@echo off
REM Shack-Bench build script — vcvars64 + configure (first time) + build.
REM
REM Works on any machine with Visual Studio (or Build Tools), CMake, Ninja and
REM Qt installed. Override the defaults by setting these before running:
REM   QT_DIR    Qt kit to build against   (default C:\Qt\6.10.3\msvc2022_64)
REM   VCVARS    path to vcvars64.bat      (default: found with vswhere)

setlocal
cd /d "%~dp0"

if "%QT_DIR%"=="" set "QT_DIR=C:\Qt\6.10.3\msvc2022_64"

if "%VCVARS%"=="" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not exist "%VCVARS%" (
    echo Could not find vcvars64.bat. Install Visual Studio C++ build tools,
    echo or set VCVARS to its full path.
    exit /b 1
)
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo Qt not found at %QT_DIR%. Set QT_DIR to your msvc2022_64 Qt kit.
    exit /b 1
)

call "%VCVARS%" >nul 2>&1

REM CMake and Ninja: use whatever is on PATH; fall back to the usual installs.
where cmake >nul 2>&1 || set "PATH=%PATH%;%ProgramFiles%\CMake\bin"
where ninja >nul 2>&1 || for /d %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*") do set "PATH=%PATH%;%%d"

if not exist build\CMakeCache.txt (
    echo Configuring with CMake against %QT_DIR% ...
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
    if errorlevel 1 goto :end
)

cmake --build build -j %NUMBER_OF_PROCESSORS% %*

:end
echo.
echo Build exit code: %errorlevel%
endlocal & exit /b %errorlevel%
