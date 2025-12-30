/**
 * @file aws_iot_client.c
 * @brief ESP32-S3 AWS IoT Core MQTT client with TLS
 *
 * This application connects an ESP32-S3 to AWS IoT Core using MQTT over TLS.
 * Features:
 * - WiFi connection management
 * - AWS IoT device authentication with certificates
 * - Device shadow synchronization
 * - Telemetry data publishing
 * - Command reception and processing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aws_iot_config.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "certificate_manager.h"
#include "device_shadow.h"
#include "wifi_manager.h"

#include "mqtt_client.h"

static const char *TAG = "AWS_IOT_CLIENT";

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Event group for synchronization
static EventGroupHandle_t aws_iot_event_group;
const int AWS_IOT_CONNECTED_BIT = BIT0;

// Configuration - Use hardcoded values for now
#define AWS_IOT_MQTT_HOST CONFIG_AWS_IOT_MQTT_HOST
#define AWS_IOT_MQTT_PORT CONFIG_AWS_IOT_MQTT_PORT
#define AWS_IOT_DEVICE_THING_NAME CONFIG_AWS_IOT_DEVICE_THING_NAME

// MQTT Topics
#define TELEMETRY_TOPIC_FMT "device/%s/telemetry"
#define COMMAND_TOPIC_FMT "device/%s/commands"
#define SHADOW_UPDATE_TOPIC_FMT "$aws/things/%s/shadow/update"
#define SHADOW_GET_TOPIC_FMT "$aws/things/%s/shadow/get"

// Task parameters
#define TELEMETRY_TASK_STACK_SIZE 4096
#define TELEMETRY_TASK_PRIORITY 5
#define TELEMETRY_PUBLISH_INTERVAL 30000 // 30 seconds

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) 
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) 
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);

        // Subscribe to command topic
        char command_topic[128];
        snprintf(command_topic, sizeof(command_topic), COMMAND_TOPIC_FMT,
                 AWS_IOT_DEVICE_THING_NAME);
        msg_id = esp_mqtt_client_subscribe(client, command_topic, 1);
        ESP_LOGI(TAG, "Subscribed to commands, msg_id=%d", msg_id);

        // Subscribe to shadow responses
        char shadow_get_accepted[256];
        snprintf(shadow_get_accepted, sizeof(shadow_get_accepted),
                 "$aws/things/%s/shadow/get/accepted",
                 AWS_IOT_DEVICE_THING_NAME);
        msg_id = esp_mqtt_client_subscribe(client, shadow_get_accepted, 1);
        ESP_LOGI(TAG, "Subscribed to shadow get accepted, msg_id=%d", msg_id);

        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("Topic: %.*s\n", event->topic_len, event->topic);
        printf("Data: %.*s\n", event->data_len, event->data);

        // Handle command messages
        char cmd_topic[128];
        snprintf(cmd_topic, sizeof(cmd_topic), COMMAND_TOPIC_FMT,
                 AWS_IOT_DEVICE_THING_NAME);
        if (strncmp(event->topic, cmd_topic, event->topic_len) == 0) {
            ESP_LOGI(TAG, "Received command: %.*s", event->data_len,
                     event->data);
            // TODO: Process commands
        }

        // Handle shadow responses
        if (strstr(event->topic, "/shadow/") != NULL) {
            device_shadow_handle_response(event->topic, event->topic_len,
                                          event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x",
                     event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x",
                     event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)",
                     event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type ==
                   MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x",
                     event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x",
                     event->error_handle->error_type);
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/**
 * @brief Initialize and start MQTT client
 */
static void mqtt_client_init(void)
{
    // Get certificates
    const char *root_ca = certificate_manager_get_root_ca();
    const char *client_cert = certificate_manager_get_client_cert();
    const char *client_key = certificate_manager_get_client_key();

    if (!root_ca || !client_cert || !client_key) {
        ESP_LOGE(TAG, "Failed to load certificates");
        return;
    }

    char mqtt_uri[256];
    snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtts://%s:%d", AWS_IOT_MQTT_HOST,
             AWS_IOT_MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker =
            {
                .address.uri = mqtt_uri,
                .verification.certificate = root_ca,
            },
        .credentials =
            {
                .authentication =
                    {
                        .certificate = client_cert,
                        .key = client_key,
                    },
            },
        .session =
            {
                .keepalive = 60,
                .disable_clean_session = false,
            },
        .network =
            {
                .timeout_ms = 5000,
                .refresh_connection_after_ms = 20000,
            },
        .buffer =
            {
                .size = 1024,
                .out_size = 1024,
            },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/**
 * @brief Telemetry publishing task
 */
static void telemetry_task(void *pvParameters) 
{
    char telemetry_topic[128];
    char telemetry_data[256];
    static int message_count = 0;

    snprintf(telemetry_topic, sizeof(telemetry_topic), TELEMETRY_TOPIC_FMT,
             AWS_IOT_DEVICE_THING_NAME);

    while (1) {
        // Wait for MQTT connection
        xEventGroupWaitBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT, false,
                            true, portMAX_DELAY);

        // Create telemetry message
        snprintf(telemetry_data, sizeof(telemetry_data),
                 "{"
                 "\"timestamp\": %lld,"
                 "\"device_id\": \"%s\","
                 "\"message_count\": %d,"
                 "\"free_heap\": %lu,"
                 "\"uptime_ms\": %llu"
                 "}",
                 esp_timer_get_time() / 1000, AWS_IOT_DEVICE_THING_NAME,
                 ++message_count, esp_get_free_heap_size(),
                 esp_timer_get_time() / 1000);

        // Publish telemetry
        int msg_id = esp_mqtt_client_publish(mqtt_client, telemetry_topic,
                                             telemetry_data, 0, 1, 0);
        ESP_LOGI(TAG, "Published telemetry, msg_id=%d", msg_id);

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PUBLISH_INTERVAL));
    }
}

/**
 * @brief Application main entry point
 */
void app_main(void) 
{
    ESP_LOGI(TAG, "ESP32-S3 AWS IoT Client Starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create event group
    aws_iot_event_group = xEventGroupCreate();

    // Initialize certificates
    certificate_manager_init();

    // Initialize WiFi
    wifi_manager_init();
    wifi_manager_connect();

    // Wait for WiFi connection
    wifi_manager_wait_for_connection();
    ESP_LOGI(TAG, "WiFi connected, starting AWS IoT client");

    // Initialize device shadow
    device_shadow_init(AWS_IOT_DEVICE_THING_NAME);

    // Initialize MQTT client
    mqtt_client_init();

    // Create telemetry task
    xTaskCreate(telemetry_task, "telemetry_task", TELEMETRY_TASK_STACK_SIZE,
                NULL, TELEMETRY_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "AWS IoT client initialized");
}