#!/bin/bash
# ESP32-S3 AWS IoT MQTT Build Script for Linux/macOS
# Automated build script with ESP-IDF environment setup
# Supports both WiFi and SIM7600E modes

set -e

echo "================================================"
echo "ESP32-S3 AWS IoT MQTT TLS Build Script"
echo "================================================"

# Parse command line arguments
BUILD_MODE="wifi"
CLEAN_BUILD=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --mode|-m)
            BUILD_MODE="$2"
            shift 2
            ;;
        --clean|-c)
            CLEAN_BUILD=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo ""
            echo "Usage: $0 [--mode MODE] [--clean]"
            echo "  --mode, -m    Build mode: wifi or sim7600e (default: wifi)"
            echo "  --clean, -c   Clean build before building"
            exit 1
            ;;
    esac
done

# Validate build mode
if [[ "$BUILD_MODE" != "wifi" && "$BUILD_MODE" != "sim7600e" ]]; then
    echo "ERROR: Invalid build mode '$BUILD_MODE'"
    echo "Valid modes: wifi, sim7600e"
    echo ""
    echo "Usage: $0 [--mode MODE] [--clean]"
    echo "  --mode, -m    Build mode: wifi or sim7600e (default: wifi)"
    echo "  --clean, -c   Clean build before building"
    exit 1
fi

echo "Build Mode: $BUILD_MODE"
echo ""

# Check if ESP-IDF is already in environment
if [ -z "$IDF_PATH" ]; then
    # Common ESP-IDF installation paths
    ESP_IDF_PATHS=(
        "$HOME/esp/esp-idf"
        "$HOME/esp/v5.3.2/esp-idf"
        "$HOME/esp/v5.3.1/esp-idf"
        "$HOME/esp/v5.3/esp-idf"
        "/opt/esp-idf"
    )
    
    echo "Searching for ESP-IDF installation..."
    found=0
    for path in "${ESP_IDF_PATHS[@]}"; do
        if [ -f "$path/export.sh" ]; then
            echo "Found ESP-IDF at: $path"
            export IDF_PATH="$path"
            found=1
            break
        fi
    done
    
    if [ $found -eq 0 ]; then
        echo "ERROR: ESP-IDF not found in standard locations"
        echo "Please install ESP-IDF or set IDF_PATH manually"
        echo "Standard locations checked:"
        printf '%s\n' "${ESP_IDF_PATHS[@]}"
        exit 1
    fi
    
    echo "Setting up ESP-IDF environment..."
    source "$IDF_PATH/export.sh"
else
    echo "ESP-IDF already configured: $IDF_PATH"
fi

echo "Current directory: $PWD"
echo "IDF_PATH: $IDF_PATH"
echo "IDF_TARGET: ${IDF_TARGET:-not set}"

# Set target to ESP32-S3
echo "Setting target to ESP32-S3..."
idf.py set-target esp32s3

# Configure build mode
echo "Configuring for $BUILD_MODE mode..."
if [ "$BUILD_MODE" = "wifi" ]; then
    echo "CONFIG_AWS_IOT_USE_WIFI=y" > sdkconfig.mode
    echo "CONFIG_AWS_IOT_USE_SIM7600E=n" >> sdkconfig.mode
else
    echo "CONFIG_AWS_IOT_USE_SIM7600E=y" > sdkconfig.mode
    echo "CONFIG_AWS_IOT_USE_WIFI=n" >> sdkconfig.mode
fi

# Apply configuration
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.mode" reconfigure

# Clean previous build if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    echo "Cleaning previous build..."
    idf.py clean
fi

# Build the project
echo "Building project..."
idf.py build

# Cleanup temporary config file
rm -f sdkconfig.mode

echo "================================================"
echo "Build completed successfully!"
echo "================================================"
echo ""
echo "Mode: $BUILD_MODE"
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