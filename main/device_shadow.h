/**
 * @file device_shadow.h
 * @brief AWS IoT Device Shadow management header
 */

#ifndef DEVICE_SHADOW_H
#define DEVICE_SHADOW_H

#include "esp_err.h"
#include "mqtt_client.h"

/**
 * @brief Initialize device shadow manager
 * @param thing_name AWS IoT Thing name
 * @return ESP_OK on success
 */
esp_err_t device_shadow_init(const char *thing_name);

/**
 * @brief Create a device state document
 * @return Allocated JSON string (must be freed by caller)
 */
char* device_shadow_create_state_document(void);

/**
 * @brief Handle shadow response from AWS IoT
 * @param topic MQTT topic
 * @param topic_len Topic length
 * @param data Message data
 * @param data_len Data length
 * @return ESP_OK on success
 */
esp_err_t device_shadow_handle_response(const char *topic, size_t topic_len, 
                                       const char *data, size_t data_len);

/**
 * @brief Update reported state in device shadow
 * @param client MQTT client handle
 * @param key State key
 * @param value State value
 * @return ESP_OK on success
 */
esp_err_t device_shadow_update_reported_state(esp_mqtt_client_handle_t client, 
                                              const char *key, const char *value);

/**
 * @brief Get the configured thing name
 * @return Thing name string
 */
const char* device_shadow_get_thing_name(void);

#endif // DEVICE_SHADOW_H