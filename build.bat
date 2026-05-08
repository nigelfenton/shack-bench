@echo off
REM TCI Monitor build script — vcvars64 + configure (first time) + build.

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=%PATH%;C:\Program Files\CMake\bin;C:\Users\nigel\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\Program Files\LLVM\bin

if not exist build\CMakeCache.txt (
    echo Configuring with CMake...
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64"
    if errorlevel 1 goto :end
)

cmake --build build -j %NUMBER_OF_PROCESSORS% %*

:end
echo.
echo Build exit code: %errorlevel%
