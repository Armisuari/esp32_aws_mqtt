#!/bin/bash
# ESP32-S3 AWS IoT MQTT Build Script for Linux/Mac
# Automated build script with ESP-IDF environment setup

echo "================================================"
echo "ESP32-S3 AWS IoT MQTT TLS Build Script"
echo "================================================"

# Check if we're running in Git Bash/MSYS2 on Windows
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "mingw"* ]]; then
    echo "WARNING: Git Bash/MSYS2 is not officially supported by ESP-IDF"
    echo "Please use one of the following alternatives:"
    echo "  1. Windows Command Prompt or PowerShell with build.bat"
    echo "  2. Windows Terminal with PowerShell"
    echo "  3. Native Linux environment or WSL2"
    echo ""
    echo "Redirecting to Windows build script..."
    echo "================================================"
    
    # Convert current directory to Windows path format
    WIN_PWD=$(pwd -W 2>/dev/null || pwd | sed 's|^/c/|C:/|' | sed 's|/|\\|g')
    
    # Try to run the Windows batch script instead
    if [ -f "build.bat" ]; then
        echo "Running build.bat in Windows environment..."
        cmd.exe /c "cd /d \"$WIN_PWD\" && build.bat"
        exit $?
    else
        echo "ERROR: build.bat not found"
        echo "Please run this script from a supported environment"
        exit 1
    fi
fi

# Check if ESP-IDF is already configured
if [ -n "$IDF_PATH" ]; then
    echo "ESP-IDF already configured: $IDF_PATH"
    # Jump to build section
    # (function equivalent to goto build in batch)
else
    # Common ESP-IDF installation paths
    ESP_IDF_PATHS=(
        "$HOME/esp/esp-idf"
        "/opt/esp-idf"
        "$HOME/.espressif/esp-idf"
        "/usr/local/esp-idf"
        "$HOME/esp/v5.3.2/esp-idf"
        "$HOME/esp/v5.3.1/esp-idf"
        "$HOME/esp/v5.3/esp-idf"
    )

    echo "Searching for ESP-IDF installation..."
    IDF_FOUND=0
    for path in "${ESP_IDF_PATHS[@]}"; do
        if [ -f "$path/export.sh" ]; then
            echo "Found ESP-IDF at: $path"
            export IDF_PATH="$path"
            IDF_FOUND=1
            break
        fi
    done

    if [ $IDF_FOUND -eq 0 ]; then
        echo "ERROR: ESP-IDF not found in standard locations"
        echo "Please install ESP-IDF or set IDF_PATH manually"
        echo "Standard locations checked:"
        for path in "${ESP_IDF_PATHS[@]}"; do
            echo "  $path"
        done
        exit 1
    fi

    echo "Setting up ESP-IDF environment..."

    # Set up Python environment first (detect Windows vs Linux)
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "mingw"* ]]; then
        # Windows (Git Bash/MSYS2)
        PYTHON_ENV="$HOME/.espressif/python_env/idf5.3_py3.11_env"
        if [ -d "$PYTHON_ENV" ]; then
            echo "Found Python environment: $PYTHON_ENV"
            export PATH="$PYTHON_ENV/Scripts:$PATH"
            export PYTHON="$PYTHON_ENV/Scripts/python.exe"
        fi
    else
        # Linux/Mac
        PYTHON_ENV="$HOME/.espressif/python_env/idf5.3_py3.11_env"
        if [ -d "$PYTHON_ENV" ]; then
            echo "Found Python environment: $PYTHON_ENV"
            export PATH="$PYTHON_ENV/bin:$PATH"
            export PYTHON="$PYTHON_ENV/bin/python"
        fi
    fi

    source "$IDF_PATH/export.sh"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to setup ESP-IDF environment"
        exit 1
    fi
fi

echo "Current directory: $(pwd)"
echo "IDF_PATH: $IDF_PATH"
echo "IDF_TARGET: ${IDF_TARGET:-not set}"

# Set target to ESP32-S3
echo "Setting target to ESP32-S3..."
idf.py set-target esp32s3
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to set target to ESP32-S3"
    exit 1
fi

# Clean previous build
echo "Cleaning previous build..."
idf.py clean

# Build the project
echo "Building project..."
idf.py build
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo "================================================"
echo "Build completed successfully!"
echo "================================================"
echo ""
echo "To flash the firmware:"
echo "  idf.py flash"
echo ""
echo "To monitor serial output:"
echo "  idf.py monitor"
echo ""
echo "To flash and monitor:"
echo "  idf.py flash monitor"
echo "================================================"