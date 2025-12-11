@echo off
REM ESP32-S3 AWS IoT MQTT Build Script for Windows
REM Automated build script with ESP-IDF environment setup

echo ================================================
echo ESP32-S3 AWS IoT MQTT TLS Build Script
echo ================================================

REM Check if ESP-IDF is already in environment
if defined IDF_PATH (
    echo ESP-IDF already configured: %IDF_PATH%
    goto build
)

REM Common ESP-IDF installation paths
set ESP_IDF_PATHS=C:\esp\esp-idf C:\Espressif\frameworks\esp-idf-v5.3.2 C:\Espressif\frameworks\esp-idf-v5.3.1 C:\Espressif\frameworks\esp-idf-v5.3 C:\Users\%USERNAME%\esp\esp-idf C:\Users\%USERNAME%\esp\v5.3.2\esp-idf

echo Searching for ESP-IDF installation...
for %%p in (%ESP_IDF_PATHS%) do (
    if exist "%%p\export.bat" (
        echo Found ESP-IDF at: %%p
        set IDF_PATH=%%p
        goto setup_env
    )
)

echo ERROR: ESP-IDF not found in standard locations
echo Please install ESP-IDF or set IDF_PATH manually
echo Standard locations checked:
for %%p in (%ESP_IDF_PATHS%) do echo   %%p
pause
exit /b 1

:setup_env
echo Setting up ESP-IDF environment...

REM Set up Python environment first
set PYTHON_ENV=%USERPROFILE%\.espressif\python_env\idf5.3_py3.11_env
if exist "%PYTHON_ENV%" (
    echo Found Python environment: %PYTHON_ENV%
    set PATH=%PYTHON_ENV%\Scripts;%PATH%
    set PYTHON=%PYTHON_ENV%\Scripts\python.exe
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 (
    echo ERROR: Failed to setup ESP-IDF environment
    pause
    exit /b 1
)

:build
echo Current directory: %CD%
echo IDF_PATH: %IDF_PATH%
echo IDF_TARGET: %IDF_TARGET%

REM Set target to ESP32-S3
echo Setting target to ESP32-S3...
idf.py set-target esp32s3
if errorlevel 1 (
    echo ERROR: Failed to set target to ESP32-S3
    pause
    exit /b 1
)

REM Clean previous build
echo Cleaning previous build...
idf.py clean

REM Build the project
echo Building project...
idf.py build
if errorlevel 1 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo ================================================
echo Build completed successfully!
echo ================================================
echo.
echo To flash the firmware:
echo   idf.py flash
echo.
echo To monitor serial output:
echo   idf.py monitor
echo.
echo To flash and monitor:
echo   idf.py flash monitor
echo ================================================
pause