/**
 * @file device_shadow_sim7600e.h
 * @brief AWS IoT Device Shadow header for SIM7600E
 */

#ifndef DEVICE_SHADOW_SIM7600E_H
#define DEVICE_SHADOW_SIM7600E_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device shadow state structure
 */
typedef struct {
    char mac_address[13];   ///< Device MAC address
    int signal_strength;    ///< Signal strength in dBm
    uint32_t heartbeat;     ///< Heartbeat counter
    bool digital_inputs[4]; ///< Digital input states (D0-D3)
    bool relay_output;      ///< Relay output state
    int temperature;        ///< Temperature reading (°C)
    int humidity;           ///< Humidity reading (%)
} device_shadow_state_t;

/**
 * @brief Shadow update callback function type
 *
 * @param state Updated shadow state
 */
typedef void (*device_shadow_callback_t)(const device_shadow_state_t *state);

/**
 * @brief Initialize device shadow with thing name
 *
 * @param thing_name AWS IoT Thing name
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_init(const char *thing_name);

/**
 * @brief Set shadow callback function
 *
 * @param callback Callback function to handle shadow updates
 */
void device_shadow_sim7600e_set_callback(device_shadow_callback_t callback);

/**
 * @brief Update shadow reported state
 *
 * @param state Shadow state to report
 * @return esp_err_t
 */
esp_err_t
device_shadow_sim7600e_update_reported(const device_shadow_state_t *state);

/**
 * @brief Get current shadow desired state
 *
 * @param state Buffer to store desired state
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_get_desired(device_shadow_state_t *state);

/**
 * @brief Publish shadow update to AWS IoT
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_publish_update(void);

/**
 * @brief Request current shadow from AWS IoT
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_get_shadow(void);

/**
 * @brief Subscribe to shadow delta updates
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_subscribe_delta(void);

/**
 * @brief Handle incoming shadow message
 *
 * @param topic MQTT topic
 * @param payload Message payload
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_handle_message(const char *topic,
                                                const char *payload);

/**
 * @brief Cleanup shadow resources
 */
void device_shadow_sim7600e_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_SHADOW_SIM7600E_H