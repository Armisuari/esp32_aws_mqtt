/**
 * @file aws_iot_config_sim7600e.h
 * @brief AWS IoT configuration header for SIM7600E implementation
 */

#ifndef AWS_IOT_CONFIG_SIM7600E_H
#define AWS_IOT_CONFIG_SIM7600E_H

// Include the base AWS IoT configuration
#include "aws_iot_config.h"

// SIM7600E specific configuration
#define SIM7600E_DEFAULT_APN "internet"
#define SIM7600E_DEFAULT_UART_PORT UART_NUM_2
#define SIM7600E_DEFAULT_TX_PIN 2
#define SIM7600E_DEFAULT_RX_PIN 1
#define SIM7600E_DEFAULT_PWRKEY_PIN 41
#define SIM7600E_DEFAULT_BAUD_RATE 115200

// MQTT configuration for SIM7600E
#define SIM7600E_MQTT_CLIENT_ID_PREFIX "esp32s3_sim7600e_"
#define SIM7600E_MQTT_KEEP_ALIVE_INTERVAL 60
#define SIM7600E_MQTT_QOS_LEVEL 1

// Telemetry configuration
#define SIM7600E_TELEMETRY_PUBLISH_INTERVAL_MS 60000 // 1 minute
#define SIM7600E_SHADOW_UPDATE_INTERVAL_MS 30000     // 30 seconds

// GPIO configuration for digital inputs and relay output
#define SIM7600E_GPIO_D0 36
#define SIM7600E_GPIO_D1 35
#define SIM7600E_GPIO_D2 32
#define SIM7600E_GPIO_D3 33
#define SIM7600E_GPIO_RELAY 4

// Network configuration
#define SIM7600E_NETWORK_REGISTRATION_TIMEOUT_MS 60000
#define SIM7600E_INTERNET_CONNECTION_TIMEOUT_MS 30000

// Certificate management
#define SIM7600E_USE_EMBEDDED_CERTIFICATES 1

#endif // AWS_IOT_CONFIG_SIM7600E_H