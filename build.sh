#!/bin/bash
# ESP32-S3 AWS IoT MQTT Build Script for Linux/Mac
# Automated build script with ESP-IDF environment setup

echo "================================================"
echo "ESP32-S3 AWS IoT MQTT TLS Build Script"
echo "================================================"

# Check if ESP-IDF is already configured
if [ -n "$IDF_PATH" ]; then
    echo "ESP-IDF already configured: $IDF_PATH"
else
    # Common ESP-IDF installation paths
    ESP_IDF_PATHS=(
        "$HOME/esp/esp-idf"
        "/opt/esp-idf"
        "$HOME/.espressif/esp-idf"
        "/usr/local/esp-idf"
    )

    echo "Searching for ESP-IDF installation..."
    for path in "${ESP_IDF_PATHS[@]}"; do
        if [ -f "$path/export.sh" ]; then
            echo "Found ESP-IDF at: $path"
            export IDF_PATH="$path"
            break
        fi
    done

    if [ -z "$IDF_PATH" ]; then
        echo "ERROR: ESP-IDF not found in standard locations"
        echo "Please install ESP-IDF or set IDF_PATH manually"
        echo "Standard locations checked:"
        for path in "${ESP_IDF_PATHS[@]}"; do
            echo "  $path"
        done
        exit 1
    fi

    echo "Setting up ESP-IDF environment..."
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