@echo off
REM Build script for SIM7600E implementation only

echo ================================================
echo ESP32-S3 AWS IoT SIM7600E Build Script  
echo ================================================

REM Set configuration for SIM7600E
echo Setting configuration for SIM7600E implementation...
echo CONFIG_AWS_IOT_USE_SIM7600E=y > temp_config.txt
echo CONFIG_AWS_IOT_USE_WIFI=n >> temp_config.txt

REM Apply configuration
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;temp_config.txt" reconfigure

REM Build
echo Building SIM7600E implementation...
idf.py build

REM Cleanup
del temp_config.txt

echo ================================================
echo SIM7600E Build Complete!
echo ================================================
echo To flash: idf.py flash
echo To monitor: idf.py monitor  
echo To flash and monitor: idf.py flash monitor
echo ================================================

pause