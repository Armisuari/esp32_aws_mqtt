/**
 * @file device_shadow.c
 * @brief AWS IoT Device Shadow management for ESP32-S3
 */

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device_shadow.h"

static const char *TAG = "DEVICE_SHADOW";
static char device_thing_name[64] = {0};

esp_err_t device_shadow_init(const char *thing_name)
{
    if (thing_name == NULL) {
        ESP_LOGE(TAG, "Thing name cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(device_thing_name, thing_name, sizeof(device_thing_name) - 1);
    device_thing_name[sizeof(device_thing_name) - 1] = '\0';
    
    ESP_LOGI(TAG, "Device shadow initialized for thing: %s", device_thing_name);
    return ESP_OK;
}

char* device_shadow_create_state_document(void)
{
    cJSON *shadow = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();
    cJSON *reported = cJSON_CreateObject();
    
    if (shadow == NULL || state == NULL || reported == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON objects");
        goto error;
    }
    
    // Add device state information
    cJSON *connected = cJSON_CreateBool(true);
    cJSON *timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON *free_heap = cJSON_CreateNumber(esp_get_free_heap_size());
    cJSON *uptime = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    
    if (connected == NULL || timestamp == NULL || free_heap == NULL || uptime == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON values");
        goto error;
    }
    
    cJSON_AddItemToObject(reported, "connected", connected);
    cJSON_AddItemToObject(reported, "timestamp", timestamp);
    cJSON_AddItemToObject(reported, "free_heap", free_heap);
    cJSON_AddItemToObject(reported, "uptime_ms", uptime);
    
    cJSON_AddItemToObject(state, "reported", reported);
    cJSON_AddItemToObject(shadow, "state", state);
    
    char *json_string = cJSON_Print(shadow);
    cJSON_Delete(shadow);
    
    return json_string;

error:
    if (shadow) cJSON_Delete(shadow);
    if (state) cJSON_Delete(state);
    if (reported) cJSON_Delete(reported);
    return NULL;
}

esp_err_t device_shadow_handle_response(const char *topic, size_t topic_len, 
                                      const char *data, size_t data_len)
{
    if (topic == NULL || data == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Handling shadow response from topic: %.*s", topic_len, topic);
    
    // Parse JSON response
    char *data_copy = malloc(data_len + 1);
    if (data_copy == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for data copy");
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(data_copy, data, data_len);
    data_copy[data_len] = '\0';
    
    cJSON *json = cJSON_Parse(data_copy);
    free(data_copy);
    
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Check for shadow document
    cJSON *state = cJSON_GetObjectItem(json, "state");
    if (state != NULL) {
        cJSON *desired = cJSON_GetObjectItem(state, "desired");
        if (desired != NULL) {
            ESP_LOGI(TAG, "Processing desired state");
            
            // Handle desired state changes
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, desired) {
                if (cJSON_IsString(item)) {
                    ESP_LOGI(TAG, "Desired %s: %s", item->string, item->valuestring);
                } else if (cJSON_IsNumber(item)) {
                    ESP_LOGI(TAG, "Desired %s: %f", item->string, item->valuedouble);
                } else if (cJSON_IsBool(item)) {
                    ESP_LOGI(TAG, "Desired %s: %s", item->string, 
                             cJSON_IsTrue(item) ? "true" : "false");
                }
            }
        }
        
        cJSON *reported = cJSON_GetObjectItem(state, "reported");
        if (reported != NULL) {
            ESP_LOGI(TAG, "Current reported state received");
        }
    }
    
    // Check for metadata
    cJSON *metadata = cJSON_GetObjectItem(json, "metadata");
    if (metadata != NULL) {
        cJSON *desired_meta = cJSON_GetObjectItem(metadata, "desired");
        if (desired_meta != NULL) {
            ESP_LOGI(TAG, "Shadow metadata received");
        }
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t device_shadow_update_reported_state(esp_mqtt_client_handle_t client, 
                                             const char *key, const char *value)
{
    if (client == NULL || key == NULL || value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for shadow update");
        return ESP_ERR_INVALID_ARG;
    }
    
    char topic[256];
    snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", device_thing_name);
    
    cJSON *shadow = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();
    cJSON *reported = cJSON_CreateObject();
    
    if (shadow == NULL || state == NULL || reported == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON objects for shadow update");
        goto error;
    }
    
    cJSON *json_value = cJSON_CreateString(value);
    if (json_value == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON value");
        goto error;
    }
    
    cJSON_AddItemToObject(reported, key, json_value);
    cJSON_AddItemToObject(state, "reported", reported);
    cJSON_AddItemToObject(shadow, "state", state);
    
    char *json_string = cJSON_Print(shadow);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        goto error;
    }
    
    int msg_id = esp_mqtt_client_publish(client, topic, json_string, 0, 1, 0);
    ESP_LOGI(TAG, "Published shadow update for %s, msg_id=%d", key, msg_id);
    
    cJSON_free(json_string);
    cJSON_Delete(shadow);
    return ESP_OK;

error:
    if (shadow) cJSON_Delete(shadow);
    if (state) cJSON_Delete(state);
    if (reported) cJSON_Delete(reported);
    return ESP_FAIL;
}

const char* device_shadow_get_thing_name(void)
{
    return device_thing_name;
}