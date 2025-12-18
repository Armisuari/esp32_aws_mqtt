/**
 * @file device_shadow_sim7600e.c
 * @brief AWS IoT Device Shadow implementation for SIM7600E
 *
 * This module provides device shadow functionality specifically for the
 * SIM7600E cellular module, using AT commands for MQTT communication with AWS
 * IoT Core.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "device_shadow_sim7600e.h"
#include "sim7600e_gsm.h"

static const char *TAG = "SHADOW_SIM7600E";

// Shadow state structure
typedef struct {
    char device_id[64];
    char mac_address[13];
    int signal_strength;
    uint32_t heartbeat;
    bool digital_inputs[4];
    bool relay_output;
    int temperature;
    int humidity;
    uint64_t timestamp;
} shadow_state_t;

// Current shadow state
static shadow_state_t current_state = {0};
static shadow_state_t desired_state = {0};

// Synchronization mutex
static SemaphoreHandle_t shadow_mutex = NULL;

// Shadow callback function
static device_shadow_callback_t shadow_callback = NULL;

// Topics
static char shadow_update_topic[128] = {0};
static char shadow_get_topic[128] = {0};
static char shadow_delta_topic[128] = {0};
static char shadow_accepted_topic[128] = {0};
static char shadow_rejected_topic[128] = {0};

/**
 * @brief Initialize device shadow with thing name
 *
 * @param thing_name AWS IoT Thing name
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_init(const char *thing_name) {
    if (!thing_name) {
        ESP_LOGE(TAG, "Thing name cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing device shadow for: %s", thing_name);

    // Create mutex for thread safety
    shadow_mutex = xSemaphoreCreateMutex();
    if (!shadow_mutex) {
        ESP_LOGE(TAG, "Failed to create shadow mutex");
        return ESP_ERR_NO_MEM;
    }

    // Setup shadow topics
    snprintf(shadow_update_topic, sizeof(shadow_update_topic),
             "$aws/things/%s/shadow/update", thing_name);
    snprintf(shadow_get_topic, sizeof(shadow_get_topic),
             "$aws/things/%s/shadow/get", thing_name);
    snprintf(shadow_delta_topic, sizeof(shadow_delta_topic),
             "$aws/things/%s/shadow/update/delta", thing_name);
    snprintf(shadow_accepted_topic, sizeof(shadow_accepted_topic),
             "$aws/things/%s/shadow/update/accepted", thing_name);
    snprintf(shadow_rejected_topic, sizeof(shadow_rejected_topic),
             "$aws/things/%s/shadow/update/rejected", thing_name);

    // Initialize current state with thing name
    strncpy(current_state.device_id, thing_name,
            sizeof(current_state.device_id) - 1);

    ESP_LOGI(TAG, "Device shadow initialized successfully");
    return ESP_OK;
}

/**
 * @brief Set shadow callback function
 *
 * @param callback Callback function to handle shadow updates
 */
void device_shadow_sim7600e_set_callback(device_shadow_callback_t callback) {
    shadow_callback = callback;
    ESP_LOGI(TAG, "Shadow callback registered");
}

/**
 * @brief Update shadow reported state
 *
 * @param state Shadow state to report
 * @return esp_err_t
 */
esp_err_t
device_shadow_sim7600e_update_reported(const device_shadow_state_t *state) {
    if (!state || !shadow_mutex) {
        ESP_LOGE(TAG, "Invalid parameters or shadow not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(shadow_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take shadow mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Update current state
    strncpy(current_state.mac_address, state->mac_address,
            sizeof(current_state.mac_address) - 1);
    current_state.signal_strength = state->signal_strength;
    current_state.heartbeat = state->heartbeat;
    memcpy(current_state.digital_inputs, state->digital_inputs,
           sizeof(current_state.digital_inputs));
    current_state.relay_output = state->relay_output;
    current_state.temperature = state->temperature;
    current_state.humidity = state->humidity;
    current_state.timestamp =
        esp_timer_get_time() / 1000000; // Convert to seconds

    xSemaphoreGive(shadow_mutex);

    ESP_LOGD(TAG, "Shadow reported state updated");
    return ESP_OK;
}

/**
 * @brief Get current shadow desired state
 *
 * @param state Buffer to store desired state
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_get_desired(device_shadow_state_t *state) {
    if (!state || !shadow_mutex) {
        ESP_LOGE(TAG, "Invalid parameters or shadow not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(shadow_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take shadow mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Copy desired state
    strncpy(state->mac_address, desired_state.mac_address,
            sizeof(state->mac_address) - 1);
    state->signal_strength = desired_state.signal_strength;
    state->heartbeat = desired_state.heartbeat;
    memcpy(state->digital_inputs, desired_state.digital_inputs,
           sizeof(state->digital_inputs));
    state->relay_output = desired_state.relay_output;
    state->temperature = desired_state.temperature;
    state->humidity = desired_state.humidity;

    xSemaphoreGive(shadow_mutex);

    return ESP_OK;
}

/**
 * @brief Create shadow JSON document
 *
 * @param reported_state Reported state to include
 * @param include_desired Whether to include desired state
 * @return char* JSON string (must be freed by caller)
 */
static char *create_shadow_json(const device_shadow_state_t *reported_state,
                                bool include_desired) {
    cJSON *root = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();

    // Add reported state
    if (reported_state) {
        cJSON *reported = cJSON_CreateObject();

        cJSON_AddStringToObject(reported, "device_id", current_state.device_id);
        cJSON_AddStringToObject(reported, "mac_address",
                                reported_state->mac_address);
        cJSON_AddNumberToObject(reported, "signal_strength",
                                reported_state->signal_strength);
        cJSON_AddNumberToObject(reported, "heartbeat",
                                reported_state->heartbeat);
        cJSON_AddBoolToObject(reported, "relay_output",
                              reported_state->relay_output);
        cJSON_AddNumberToObject(reported, "temperature",
                                reported_state->temperature);
        cJSON_AddNumberToObject(reported, "humidity", reported_state->humidity);
        cJSON_AddNumberToObject(reported, "timestamp", current_state.timestamp);

        // Add digital inputs array
        cJSON *inputs = cJSON_CreateArray();
        for (int i = 0; i < 4; i++) {
            cJSON_AddItemToArray(
                inputs, cJSON_CreateBool(reported_state->digital_inputs[i]));
        }
        cJSON_AddItemToObject(reported, "digital_inputs", inputs);

        cJSON_AddItemToObject(state, "reported", reported);
    }

    // Add desired state if requested
    if (include_desired) {
        cJSON *desired = cJSON_CreateObject();
        cJSON_AddBoolToObject(desired, "relay_output",
                              desired_state.relay_output);
        cJSON_AddItemToObject(state, "desired", desired);
    }

    cJSON_AddItemToObject(root, "state", state);

    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);

    return json_string;
}

/**
 * @brief Parse incoming shadow delta message
 *
 * @param json_payload JSON payload from shadow delta
 * @return esp_err_t
 */
static esp_err_t parse_shadow_delta(const char *json_payload) {
    cJSON *root = cJSON_Parse(json_payload);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse shadow delta JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *state = cJSON_GetObjectItem(root, "state");
    if (!state) {
        ESP_LOGE(TAG, "No 'state' object in shadow delta");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    bool state_changed = false;

    // Parse relay output change
    cJSON *relay_output = cJSON_GetObjectItem(state, "relay_output");
    if (relay_output && cJSON_IsBool(relay_output)) {
        bool new_relay_state = cJSON_IsTrue(relay_output);
        if (new_relay_state != desired_state.relay_output) {
            desired_state.relay_output = new_relay_state;
            state_changed = true;
            ESP_LOGI(TAG, "Relay output changed to: %s",
                     new_relay_state ? "ON" : "OFF");
        }
    }

    cJSON_Delete(root);

    // Trigger callback if state changed
    if (state_changed && shadow_callback) {
        device_shadow_state_t callback_state = {0};
        device_shadow_sim7600e_get_desired(&callback_state);
        shadow_callback(&callback_state);
    }

    return ESP_OK;
}

/**
 * @brief Publish shadow update to AWS IoT
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_publish_update(void) {
    if (!shadow_mutex) {
        ESP_LOGE(TAG, "Shadow not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(shadow_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take shadow mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Create current state copy for JSON generation
    device_shadow_state_t current_copy = {
        .signal_strength = current_state.signal_strength,
        .heartbeat = current_state.heartbeat,
        .relay_output = current_state.relay_output,
        .temperature = current_state.temperature,
        .humidity = current_state.humidity};
    strncpy(current_copy.mac_address, current_state.mac_address,
            sizeof(current_copy.mac_address) - 1);
    memcpy(current_copy.digital_inputs, current_state.digital_inputs,
           sizeof(current_copy.digital_inputs));

    xSemaphoreGive(shadow_mutex);

    // Create shadow JSON
    char *shadow_json = create_shadow_json(&current_copy, false);
    if (!shadow_json) {
        ESP_LOGE(TAG, "Failed to create shadow JSON");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Publishing shadow update: %s", shadow_json);

    // Publish using SIM7600E AT commands
    char response[256];
    char command[128];
    esp_err_t ret = ESP_OK;

    // Set topic
    snprintf(command, sizeof(command), "AT+CMQTTTOPIC=0,%d\r\n",
             strlen(shadow_update_topic));
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command(shadow_update_topic, response,
                                           sizeof(response), 3000);
    }

    // Set payload
    if (ret == ESP_OK) {
        snprintf(command, sizeof(command), "AT+CMQTTPAYLOAD=0,%d\r\n",
                 strlen(shadow_json));
        ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
                                           3000);
        if (ret == ESP_OK) {
            ret = sim7600e_gsm_send_at_command(shadow_json, response,
                                               sizeof(response), 3000);
        }
    }

    // Publish
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command("AT+CMQTTPUB=0,1,60\r\n", response,
                                           sizeof(response), 10000);
    }

    free(shadow_json);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Shadow update published successfully");
    } else {
        ESP_LOGE(TAG, "Failed to publish shadow update");
    }

    return ret;
}

/**
 * @brief Request current shadow from AWS IoT
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_get_shadow(void) {
    ESP_LOGI(TAG, "Requesting current shadow from AWS IoT");

    // Create empty shadow get request
    const char *get_request = "{}";

    char response[256];
    char command[128];
    esp_err_t ret = ESP_OK;

    // Set topic
    snprintf(command, sizeof(command), "AT+CMQTTTOPIC=0,%d\r\n",
             strlen(shadow_get_topic));
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command(shadow_get_topic, response,
                                           sizeof(response), 3000);
    }

    // Set payload
    if (ret == ESP_OK) {
        snprintf(command, sizeof(command), "AT+CMQTTPAYLOAD=0,%d\r\n",
                 strlen(get_request));
        ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
                                           3000);
        if (ret == ESP_OK) {
            ret = sim7600e_gsm_send_at_command(get_request, response,
                                               sizeof(response), 3000);
        }
    }

    // Publish
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command("AT+CMQTTPUB=0,1,60\r\n", response,
                                           sizeof(response), 10000);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Shadow get request sent successfully");
    } else {
        ESP_LOGE(TAG, "Failed to send shadow get request");
    }

    return ret;
}

/**
 * @brief Subscribe to shadow delta updates
 *
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_subscribe_delta(void) {
    ESP_LOGI(TAG, "Subscribing to shadow delta updates");

    char response[256];
    char command[128];
    esp_err_t ret;

    // Subscribe to shadow delta topic
    snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n",
             strlen(shadow_delta_topic));
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate delta subscription");
        return ret;
    }

    ret = sim7600e_gsm_send_at_command(shadow_delta_topic, response,
                                       sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send delta topic");
        return ret;
    }

    // Subscribe to shadow accepted topic
    snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n",
             strlen(shadow_accepted_topic));
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command(shadow_accepted_topic, response,
                                           sizeof(response), 3000);
    }

    // Subscribe to shadow rejected topic
    if (ret == ESP_OK) {
        snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n",
                 strlen(shadow_rejected_topic));
        ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
                                           3000);
        if (ret == ESP_OK) {
            ret = sim7600e_gsm_send_at_command(shadow_rejected_topic, response,
                                               sizeof(response), 3000);
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Successfully subscribed to shadow topics");
    } else {
        ESP_LOGE(TAG, "Failed to subscribe to shadow topics");
    }

    return ret;
}

/**
 * @brief Handle incoming shadow message
 *
 * @param topic MQTT topic
 * @param payload Message payload
 * @return esp_err_t
 */
esp_err_t device_shadow_sim7600e_handle_message(const char *topic,
                                                const char *payload) {
    if (!topic || !payload) {
        ESP_LOGE(TAG, "Invalid topic or payload");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Handling shadow message on topic: %s", topic);
    ESP_LOGI(TAG, "Payload: %s", payload);

    // Check if it's a shadow delta message
    if (strstr(topic, "/shadow/update/delta")) {
        return parse_shadow_delta(payload);
    }

    // Check if it's a shadow accepted message
    if (strstr(topic, "/shadow/update/accepted")) {
        ESP_LOGI(TAG, "Shadow update accepted");
        return ESP_OK;
    }

    // Check if it's a shadow rejected message
    if (strstr(topic, "/shadow/update/rejected")) {
        ESP_LOGW(TAG, "Shadow update rejected: %s", payload);
        return ESP_OK;
    }

    // Check if it's a shadow get response
    if (strstr(topic, "/shadow/get/accepted")) {
        ESP_LOGI(TAG, "Received shadow get response");
        // Parse and update local state if needed
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown shadow topic: %s", topic);
    return ESP_OK;
}

/**
 * @brief Cleanup shadow resources
 */
void device_shadow_sim7600e_cleanup(void) {
    if (shadow_mutex) {
        vSemaphoreDelete(shadow_mutex);
        shadow_mutex = NULL;
    }
    ESP_LOGI(TAG, "Device shadow cleanup completed");
}