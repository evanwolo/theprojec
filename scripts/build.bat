@echo off
REM Build script for Windows
REM Requires Visual Studio 2022 or MinGW

echo Building Grand Strategy Simulation Engine...
echo.

if not exist build mkdir build
cd build

REM Try CMake if available
where cmake >nul 2>&1
if %errorlevel% equ 0 (
    echo Using CMake...
    cmake .. -G "Visual Studio 17 2022"
    if %errorlevel% equ 0 (
        cmake --build . --config Release
        goto :success
    )
)

REM Fallback to cl.exe (MSVC) if available
where cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Using MSVC directly...
    cl /std:c++17 /O2 /EHsc /W4 /I..\include ^
       ..\src\Kernel.cpp ^
       ..\src\KernelSnapshot.cpp ^
       ..\src\main_kernel.cpp ^
       /Fe:KernelSim.exe
    if %errorlevel% equ 0 goto :success
)

REM Fallback to g++ (MinGW) if available
where g++ >nul 2>&1
if %errorlevel% equ 0 (
    echo Using MinGW g++...
    g++ -std=c++17 -O3 -march=native -Wall -Wextra -I..\include ^
        ..\src\Kernel.cpp ^
        ..\src\KernelSnapshot.cpp ^
        ..\src\main_kernel.cpp ^
        -o KernelSim.exe
    if %errorlevel% equ 0 goto :success
)

echo ERROR: No suitable compiler found!
echo Please install one of: CMake + Visual Studio, Visual Studio, or MinGW
goto :end

:success
echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Run the kernel simulation:
if exist Release\KernelSim.exe (
    echo   Release\KernelSim.exe
) else if exist KernelSim.exe (
    echo   KernelSim.exe
)
echo.

:end
cd ..
