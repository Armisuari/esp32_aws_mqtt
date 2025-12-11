# ESP32-S3 AWS IoT MQTT Project
cmake_minimum_required(VERSION 3.16)

# Set the ESP32 target before project()
set(SUPPORTED_TARGETS esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_aws_mqtt)