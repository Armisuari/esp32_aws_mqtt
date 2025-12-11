/**
 * @file wifi_manager.h
 * @brief WiFi connection management header
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initialize WiFi manager
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to WiFi network
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Wait for WiFi connection to be established
 */
void wifi_manager_wait_for_connection(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

#endif // WIFI_MANAGER_H