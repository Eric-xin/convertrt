@echo off
REM Windows deployment script for convertrt
REM Usage: deploy-windows.bat [build_dir] [qt_dir]

setlocal enabledelayedexpansion

REM Set default paths
set BUILD_DIR=%1
set QT_DIR=%2

if "%BUILD_DIR%"=="" set BUILD_DIR=build\Release
if "%QT_DIR%"=="" (
    REM Try to find Qt installation
    for /d %%i in ("C:\Qt\6.*") do set QT_DIR=%%i\msvc2019_64
    if "!QT_DIR!"=="" (
        echo Error: Qt directory not found. Please specify Qt directory as second argument.
        echo Usage: deploy-windows.bat [build_dir] [qt_dir]
        exit /b 1
    )
)

echo Deploying convertrt from %BUILD_DIR% using Qt from %QT_DIR%

REM Create deployment directory
if exist deploy rmdir /s /q deploy
mkdir deploy

REM Check if executable exists
if not exist "%BUILD_DIR%\convertrt.exe" (
    echo Error: convertrt.exe not found in %BUILD_DIR%
    echo Make sure to build the project first with: cmake --build build --config Release
    exit /b 1
)

REM Copy the executable
copy "%BUILD_DIR%\convertrt.exe" "deploy\"

REM Copy config file if it exists
if exist "config.ini" copy "config.ini" "deploy\"

REM Add Qt bin directory to PATH
set PATH=%QT_DIR%\bin;%PATH%

REM Check if windeployqt exists
where windeployqt >nul 2>&1
if errorlevel 1 (
    echo Error: windeployqt not found. Make sure Qt bin directory is in PATH.
    echo Qt bin should be at: %QT_DIR%\bin
    exit /b 1
)

echo Running windeployqt...
windeployqt.exe --release --no-translations --no-system-d3d-compiler --no-opengl-sw deploy\convertrt.exe

REM Copy Visual C++ Redistributable if needed
REM You might need to install Visual C++ Redistributable on target machines

echo.
echo Deployment complete!
echo Ready-to-use application is in the 'deploy' folder.
echo.
echo Contents:
dir deploy /b

REM Create a simple launcher script
echo @echo off > deploy\run.bat
echo REM Launcher script for convertrt >> deploy\run.bat
echo cd /d "%%~dp0" >> deploy\run.bat
echo convertrt.exe >> deploy\run.bat
echo if errorlevel 1 pause >> deploy\run.bat

echo.
echo You can run the application by:
echo 1. Double-clicking convertrt.exe in the deploy folder
echo 2. Running run.bat in the deploy folder
echo 3. Distributing the entire deploy folder to other Windows machines
