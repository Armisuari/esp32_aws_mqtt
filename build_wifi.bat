@echo off
REM Build script for WiFi implementation only

echo ================================================
echo ESP32-S3 AWS IoT WiFi Build Script
echo ================================================

REM Set configuration for WiFi
echo Setting configuration for WiFi implementation...
echo CONFIG_AWS_IOT_USE_WIFI=y > temp_config.txt
echo CONFIG_AWS_IOT_USE_SIM7600E=n >> temp_config.txt

REM Apply configuration
idf.py set-target esp32s3  
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;temp_config.txt" reconfigure

REM Build
echo Building WiFi implementation...
idf.py build

REM Cleanup
del temp_config.txt

echo ================================================
echo WiFi Build Complete!
echo ================================================
echo To flash: idf.py flash
echo To monitor: idf.py monitor
echo To flash and monitor: idf.py flash monitor  
echo ================================================

pause