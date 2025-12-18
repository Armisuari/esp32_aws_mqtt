/**
 * @file aws_iot_sim7600e.c
 * @brief AWS IoT Core MQTT client implementation using SIM7600E cellular module
 *
 * This implementation provides AWS IoT connectivity using the SIM7600E 4G
 * module instead of WiFi. It supports:
 * - Cellular connection management
 * - AWS IoT device authentication with certificates
 * - SSL/TLS MQTT connection to AWS IoT Core
 * - Device shadow synchronization
 * - Telemetry data publishing
 * - Command reception and processing
 *
 * Implementation follows the proven pattern from Arduino SIM7600 MQTT SSL
 * reference
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

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "certificate_manager.h"
#include "certificate_manager_sim7600e.h"
#include "device_shadow.h"
#include "device_shadow_sim7600e.h"
#include "driver/gpio.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"

static const char *TAG = "AWS_IOT_SIM7600E";

// Event group for synchronization
static EventGroupHandle_t aws_iot_event_group;
const int AWS_IOT_CONNECTED_BIT = BIT0;
const int AWS_IOT_SUBSCRIBED_BIT = BIT1;
const int NETWORK_READY_BIT = BIT2;
const int GPRS_READY_BIT = BIT3;

// Device and connection configuration
static char device_mac[13];
static char device_thing_name[64];
static char client_id[32];

// MQTT Topics
static char shadow_update_topic[128];
static char shadow_get_topic[128];
static char shadow_delta_topic[128];
static char telemetry_topic[128];
static char command_topic[128];

// Configuration from aws_iot_config.h
#define AWS_IOT_ENDPOINT CONFIG_AWS_IOT_MQTT_HOST
#define AWS_IOT_PORT CONFIG_AWS_IOT_MQTT_PORT
#define APN "internet" // Default APN - should be configurable

// GPIO configuration - Only relay output (digital inputs mocked)
#define GPIO_RELAY 4

// Telemetry data structure
typedef struct {
    int signal_strength;
    uint32_t heartbeat_counter;
    bool digital_inputs[4];
    bool relay_output;
} device_telemetry_t;

static device_telemetry_t telemetry_data = {0};

// Function prototypes
static esp_err_t init_gpio(void);
static void read_digital_inputs(bool *inputs);
static void shadow_callback(const device_shadow_state_t *state);
static void setup_device_identity(void);
static void setup_aws_iot_topics(void);

// Network and connection management (following reference pattern)
static esp_err_t init_network_and_gprs(void);
static bool is_network_connected(void);
static bool is_gprs_connected(void);
static esp_err_t connect_to_gprs(void);
static esp_err_t connect_to_mqtt(void);

// MQTT operations
static esp_err_t subscribe_to_aws_topics(void);
static esp_err_t publish_device_shadow(void);
static esp_err_t publish_telemetry_data(void);
static void handle_incoming_messages(void);

// Tasks
static void aws_iot_task(void *pvParameters);
static void telemetry_task(void *pvParameters);

/**
 * Initialize GPIO pins for relay output only (digital inputs are mocked)
 */
static esp_err_t init_gpio(void) {
    esp_err_t ret;

    // Configure relay output pin only
    gpio_config_t output_config = {.pin_bit_mask = (1ULL << GPIO_RELAY),
                                   .mode = GPIO_MODE_OUTPUT,
                                   .pull_up_en = GPIO_PULLUP_DISABLE,
                                   .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                   .intr_type = GPIO_INTR_DISABLE};

    ret = gpio_config(&output_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure relay GPIO");
        return ret;
    }

    // Set initial relay state to OFF
    gpio_set_level(GPIO_RELAY, 0);

    ESP_LOGI(TAG, "GPIO initialized successfully (relay only, inputs mocked)");
    return ESP_OK;
}

/**
 * Mock digital input states (simulated sensor data)
 */
static void read_digital_inputs(bool *inputs) {
    static uint32_t counter = 0;
    counter++;

    // Generate mock values for demonstration
    inputs[0] = (counter % 10) < 5;                   // Toggle every 5 cycles
    inputs[1] = (counter % 7) < 3;                    // Different pattern
    inputs[2] = (counter % 3) == 0;                   // True every 3rd cycle
    inputs[3] = (esp_timer_get_time() / 1000000) % 2; // Time-based toggle

    ESP_LOGD(TAG, "Mock inputs: D0=%d, D1=%d, D2=%d, D3=%d", inputs[0],
             inputs[1], inputs[2], inputs[3]);
}

/**
 * Shadow callback function to handle desired state changes
 */
static void shadow_callback(const device_shadow_state_t *state) {
    ESP_LOGI(TAG, "Shadow state change received:");
    ESP_LOGI(TAG, "  Relay output: %s", state->relay_output ? "ON" : "OFF");

    // Update GPIO based on desired state
    gpio_set_level(GPIO_RELAY, state->relay_output);
    telemetry_data.relay_output = state->relay_output;

    ESP_LOGI(TAG, "Applied shadow state changes");
}

/**
 * Setup device identity using MAC address
 */
static void setup_device_identity(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Create MAC-based device identifier
    snprintf(device_mac, sizeof(device_mac), "%02X%02X%02X%02X%02X%02X", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Create device thing name
    snprintf(device_thing_name, sizeof(device_thing_name), "esp32-s3-device-%s",
             device_mac);

    // Create client ID
    snprintf(client_id, sizeof(client_id), "esp32s3_%s", device_mac);

    ESP_LOGI(TAG, "Device MAC: %s", device_mac);
    ESP_LOGI(TAG, "Thing Name: %s", device_thing_name);
    ESP_LOGI(TAG, "Client ID: %s", client_id);
}

/**
 * Setup AWS IoT MQTT topics (matching WiFi implementation)
 */
static void setup_aws_iot_topics(void) {
    // Device shadow topics (matching WiFi implementation)
    snprintf(shadow_update_topic, sizeof(shadow_update_topic),
             "$aws/things/%s/shadow/update", device_thing_name);
    snprintf(shadow_get_topic, sizeof(shadow_get_topic),
             "$aws/things/%s/shadow/get", device_thing_name);
    snprintf(shadow_delta_topic, sizeof(shadow_delta_topic),
             "$aws/things/%s/shadow/update/delta", device_thing_name);

    // Custom telemetry and command topics (matching WiFi implementation)
    snprintf(telemetry_topic, sizeof(telemetry_topic), "device/%s/telemetry",
             device_thing_name);
    snprintf(command_topic, sizeof(command_topic), "device/%s/commands",
             device_thing_name);

    ESP_LOGI(TAG, "Shadow Update Topic: %s", shadow_update_topic);
    ESP_LOGI(TAG, "Shadow Delta Topic: %s", shadow_delta_topic);
    ESP_LOGI(TAG, "Telemetry Topic: %s", telemetry_topic);
    ESP_LOGI(TAG, "Command Topic: %s", command_topic);
}

/**
 * Check if network is connected (following reference pattern)
 */
static bool is_network_connected(void) {
    char response[256];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CREG?\r\n", response,
                                                 sizeof(response), 3000);

    if (ret != ESP_OK) {
        return false;
    }

    // Check for +CREG: 0,1 (registered, home network) or +CREG: 0,5
    // (registered, roaming)
    if (strstr(response, "+CREG: 0,1") != NULL ||
        strstr(response, "+CREG: 0,5") != NULL) {
        return true;
    }

    return false;
}

/**
 * Check if GPRS is connected (following reference pattern)
 */
static bool is_gprs_connected(void) {
    char response[256];
    esp_err_t ret = sim7600e_gsm_send_at_command("AT+CGATT?\r\n", response,
                                                 sizeof(response), 3000);

    if (ret != ESP_OK) {
        return false;
    }

    // Check for +CGATT: 1 (GPRS attached)
    if (strstr(response, "+CGATT: 1") != NULL) {
        return true;
    }

    return false;
}

/**
 * Initialize network and GPRS connection (following reference Init pattern)
 */
static esp_err_t init_network_and_gprs(void) {
    char response[512];
    char command[128];
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing network and GPRS...");

    // Set full functionality
    ret = sim7600e_gsm_send_at_command("AT+CFUN=1\r\n", response,
                                       sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set full functionality");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Check SIM card
    ret = sim7600e_gsm_send_at_command("AT+CPIN?\r\n", response,
                                       sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check SIM card");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Check signal quality
    ret = sim7600e_gsm_send_at_command("AT+CSQ\r\n", response, sizeof(response),
                                       1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Signal quality: %s", response);
    }

    // Check network registration
    ret = sim7600e_gsm_send_at_command("AT+CREG?\r\n", response,
                                       sizeof(response), 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Network registration: %s", response);
    }

    // Check operator
    ret = sim7600e_gsm_send_at_command("AT+COPS?\r\n", response,
                                       sizeof(response), 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Operator: %s", response);
    }

    // Check GPRS attachment
    ret = sim7600e_gsm_send_at_command("AT+CGATT?\r\n", response,
                                       sizeof(response), 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GPRS attachment: %s", response);
    }

    // Get system info
    ret = sim7600e_gsm_send_at_command("AT+CPSI?\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "System info: %s", response);
    }

    // Configure PDP context
    snprintf(command, sizeof(command), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", APN);
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PDP context");
        return ret;
    }

    // Activate PDP context
    ret = sim7600e_gsm_send_at_command("AT+CGACT=1,1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PDP activation returned error, continuing anyway");
    }

    // Check GPRS attachment again
    ret = sim7600e_gsm_send_at_command("AT+CGATT?\r\n", response,
                                       sizeof(response), 1000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GPRS attachment after activation: %s", response);
    }

    // Get PDP address
    ret = sim7600e_gsm_send_at_command("AT+CGPADDR=1\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PDP address: %s", response);
    }

    // Open network
    ret = sim7600e_gsm_send_at_command("AT+NETOPEN\r\n", response,
                                       sizeof(response), 5000);
    if (ret == ESP_OK || strstr(response, "already opened") != NULL) {
        ESP_LOGI(TAG, "Network opened");
    } else {
        ESP_LOGW(TAG, "Network open returned: %s", response);
    }

    // Check network state
    ret = sim7600e_gsm_send_at_command("AT+NETSTATE\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Network state: %s", response);
    }

    ESP_LOGI(TAG, "Network and GPRS initialization completed");
    xEventGroupSetBits(aws_iot_event_group, NETWORK_READY_BIT | GPRS_READY_BIT);

    return ESP_OK;
}

/**
 * Connect/reconnect to GPRS (following reference connectToGPRS pattern)
 */
static esp_err_t connect_to_gprs(void) {
    char response[512];
    char command[128];
    esp_err_t ret;

    ESP_LOGI(TAG, "Connecting to GPRS...");

    // Attach to GPRS
    ret = sim7600e_gsm_send_at_command("AT+CGATT=1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach to GPRS");
        return ret;
    }

    // Configure PDP context
    snprintf(command, sizeof(command), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", APN);
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PDP context");
        return ret;
    }

    // Activate PDP context
    ret = sim7600e_gsm_send_at_command("AT+CGACT=1,1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PDP activation warning, continuing");
    }

    // Get PDP address
    ret = sim7600e_gsm_send_at_command("AT+CGPADDR=1\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PDP address: %s", response);
    }

    // Open network
    ret = sim7600e_gsm_send_at_command("AT+NETOPEN\r\n", response,
                                       sizeof(response), 5000);
    if (ret == ESP_OK || strstr(response, "already opened") != NULL) {
        ESP_LOGI(TAG, "Network opened for GPRS");
    }

    // Check network state
    ret = sim7600e_gsm_send_at_command("AT+NETSTATE\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Network state after GPRS: %s", response);
    }

    ESP_LOGI(TAG, "GPRS connection established");
    xEventGroupSetBits(aws_iot_event_group, GPRS_READY_BIT);

    return ESP_OK;
}

/**
 * Connect to AWS IoT MQTT broker (following reference connectToMQTT pattern)
 */
static esp_err_t connect_to_mqtt(void) {
    char response[512];
    char command[256];
    esp_err_t ret;

    ESP_LOGI(TAG, "Connecting to AWS IoT MQTT broker...");

    // Stop any existing MQTT connection
    sim7600e_gsm_send_at_command("AT+CMQTTDISC=0,60\r\n", response, sizeof(response), 2000); // Graceful disconnect
    sim7600e_gsm_send_at_command("AT+CMQTTREL=0\r\n", response, sizeof(response), 2000); // Release client
    sim7600e_gsm_send_at_command("AT+CMQTTSTOP\r\n", response, sizeof(response), 2000); // Stop MQTT service
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Enable SSL for MQTT
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSSLCFG=0,1\r\n", response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable SSL for MQTT: %s", response);
        return ret;
    }
    ESP_LOGI(TAG, "SSL enabled for MQTT");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Configure SSL certificates (using certificate manager configuration)
    ESP_LOGI(TAG, "Configuring SSL certificates from certificate manager...");
    ret = certificate_manager_sim7600e_configure_aws_iot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SSL certificates");
        return ret;
    }
    ESP_LOGI(TAG, "SSL certificates configured successfully");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Start MQTT service
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSTART\r\n", response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT service: %s", response);
        return ret;
    }
    ESP_LOGI(TAG, "MQTT service started");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Acquire MQTT client
    snprintf(command, sizeof(command), "AT+CMQTTACCQ=0,\"%s\",1\r\n", client_id);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire MQTT client: %s", response);
        return ret;
    }
    ESP_LOGI(TAG, "MQTT client acquired: %s", client_id);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Connect to AWS IoT broker
    snprintf(command, sizeof(command), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1\r\n", AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    ESP_LOGI(TAG, "Connecting to: %s:%d", AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    ret = sim7600e_gsm_send_at_command(command, response, sizeof(response), 30000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to AWS IoT: %s", response);
        return ret;
    }

    // Check if connection was successful
    if (strstr(response, "+CMQTTCONNECT: 0,0") != NULL || strstr(response, "OK") != NULL) {
        ESP_LOGI(TAG, "Successfully connected to AWS IoT Core");
        xEventGroupSetBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "AWS IoT connection failed: %s", response);
        return ESP_FAIL;
    }
}

// /**
//  * Subscribe to AWS IoT topics
//  */
// static esp_err_t subscribe_to_aws_topics(void) {
//     ESP_LOGI(TAG, "Subscribing to AWS IoT topics...");
//     char response[512];
//     char command[128];
//     esp_err_t ret;

//     vTaskDelay(pdMS_TO_TICKS(2000));

//     // Subscribe to shadow delta topic
//     ESP_LOGI(TAG, "Subscribing to: %s", shadow_delta_topic);
//     snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n",
//              (int)strlen(shadow_delta_topic));
//     ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
//                                        5000);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to initiate delta subscription: %s", response);
//         return ret;
//     }

//     // Send topic with Ctrl+Z terminator
//     char topic_with_term[256];
//     snprintf(topic_with_term, sizeof(topic_with_term), "%s\x1A",
//     shadow_delta_topic); ret = sim7600e_gsm_send_at_command(topic_with_term,
//     response,
//                                        sizeof(response), 5000);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to send delta topic: %s", response);
//         return ret;
//     }
//     ESP_LOGI(TAG, "Shadow delta topic subscribed");
//     vTaskDelay(pdMS_TO_TICKS(2000));

//     // Subscribe to command topic
//     ESP_LOGI(TAG, "Subscribing to: %s", command_topic);
//     snprintf(command, sizeof(command), "AT+CMQTTSUB=0,%d,1\r\n",
//              (int)strlen(command_topic));
//     ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
//                                        5000);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to initiate command subscription: %s",
//         response); return ret;
//     }

//     // Send topic with Ctrl+Z terminator
//     snprintf(topic_with_term, sizeof(topic_with_term), "%s\x1A",
//     command_topic); ret = sim7600e_gsm_send_at_command(topic_with_term,
//     response,
//                                        sizeof(response), 5000);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to send command topic: %s", response);
//         return ret;
//     }
//     ESP_LOGI(TAG, "Command topic subscribed");
//     vTaskDelay(pdMS_TO_TICKS(1000));

//     ESP_LOGI(TAG, "Successfully subscribed to AWS IoT topics");
//     xEventGroupSetBits(aws_iot_event_group, AWS_IOT_SUBSCRIBED_BIT);

//     return ESP_OK;
// }

/**
 * Subscribe to AWS IoT topics using component's built-in function
 */
static esp_err_t subscribe_to_aws_topics(void) {
    ESP_LOGI(TAG, "Subscribing to AWS IoT topics...");
    esp_err_t ret;

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Subscribe to shadow delta topic using component function
    ESP_LOGI(TAG, "Subscribing to: %s", shadow_delta_topic);
    ret = sim7600e_gsm_mqtt_subscribe(shadow_delta_topic, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to shadow delta topic");
        return ret;
    }
    ESP_LOGI(TAG, "Shadow delta topic subscribed successfully");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Subscribe to command topic using component function
    ESP_LOGI(TAG, "Subscribing to: %s", command_topic);
    ret = sim7600e_gsm_mqtt_subscribe(command_topic, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to command topic");
        return ret;
    }
    ESP_LOGI(TAG, "Command topic subscribed successfully");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Successfully subscribed to all AWS IoT topics");
    xEventGroupSetBits(aws_iot_event_group, AWS_IOT_SUBSCRIBED_BIT);

    return ESP_OK;
}

/**
 * Handle incoming MQTT messages (following reference handleIncomingMessages
 * pattern)
 */
static void handle_incoming_messages(void) {
    // This function parses unsolicited responses from the SIM7600E module
    // The actual parsing happens in the background UART handler
    // Here we just yield to allow the handler to process
    vTaskDelay(pdMS_TO_TICKS(10));
}

/**
 * Publish device shadow state to AWS IoT
 */
static esp_err_t publish_device_shadow(void) {
    // Update the shadow reported state with current telemetry data
    device_shadow_state_t shadow_state = {
        .signal_strength = telemetry_data.signal_strength,
        .heartbeat = telemetry_data.heartbeat_counter,
        .relay_output = telemetry_data.relay_output,
        .temperature = 25, // TODO: Read from actual sensor
        .humidity = 60     // TODO: Read from actual sensor
    };

    // Copy MAC address and digital inputs
    strncpy(shadow_state.mac_address, device_mac,
            sizeof(shadow_state.mac_address) - 1);
    memcpy(shadow_state.digital_inputs, telemetry_data.digital_inputs,
           sizeof(shadow_state.digital_inputs));

    // Update the reported state
    esp_err_t ret = device_shadow_sim7600e_update_reported(&shadow_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update shadow reported state");
        return ret;
    }

    // Publish the shadow update
    ret = device_shadow_sim7600e_publish_update();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Device shadow published successfully");
    } else {
        ESP_LOGE(TAG, "Failed to publish device shadow");
    }

    return ret;
}

/**
 * Publish telemetry data to AWS IoT
 */
static esp_err_t publish_telemetry_data(void) {
    char response[256];
    char command[128];
    esp_err_t ret;

    // Create telemetry JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", device_thing_name);
    cJSON_AddStringToObject(root, "mac_address", device_mac);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "signal_strength",
                            telemetry_data.signal_strength);
    cJSON_AddNumberToObject(root, "heartbeat",
                            telemetry_data.heartbeat_counter);

    // Add sensor data
    cJSON *sensors = cJSON_CreateObject();
    for (int i = 0; i < 4; i++) {
        char sensor_name[8];
        snprintf(sensor_name, sizeof(sensor_name), "D%d", i);
        cJSON_AddBoolToObject(sensors, sensor_name,
                              telemetry_data.digital_inputs[i]);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    char *telemetry_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!telemetry_json) {
        ESP_LOGE(TAG, "Failed to create telemetry JSON");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Publishing telemetry: %s", telemetry_json);

    // Set topic
    snprintf(command, sizeof(command), "AT+CMQTTTOPIC=0,%d\r\n",
             (int)strlen(telemetry_topic));
    ret =
        sim7600e_gsm_send_at_command(command, response, sizeof(response), 3000);
    if (ret == ESP_OK) {
        char topic_with_term[256];
        snprintf(topic_with_term, sizeof(topic_with_term), "%s\x1A",
                 telemetry_topic);
        ret = sim7600e_gsm_send_at_command(topic_with_term, response,
                                           sizeof(response), 3000);
    }

    // Set payload
    if (ret == ESP_OK) {
        snprintf(command, sizeof(command), "AT+CMQTTPAYLOAD=0,%d\r\n",
                 (int)strlen(telemetry_json));
        ret = sim7600e_gsm_send_at_command(command, response, sizeof(response),
                                           3000);
        if (ret == ESP_OK) {
            char payload_with_term[512];
            snprintf(payload_with_term, sizeof(payload_with_term), "%s\x1A",
                     telemetry_json);
            ret = sim7600e_gsm_send_at_command(payload_with_term, response,
                                               sizeof(response), 3000);
        }
    }

    // Publish
    if (ret == ESP_OK) {
        ret = sim7600e_gsm_send_at_command("AT+CMQTTPUB=0,1,60\r\n", response,
                                           sizeof(response), 10000);
    }

    free(telemetry_json);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Telemetry published successfully");
    } else {
        ESP_LOGE(TAG, "Failed to publish telemetry: %s", response);

        // Check connection status and reconnect if needed
        if (!is_network_connected()) {
            ESP_LOGW(TAG, "Network connection lost, triggering reconnection");
            xEventGroupClearBits(aws_iot_event_group, NETWORK_READY_BIT);
        } else if (!is_gprs_connected()) {
            ESP_LOGW(TAG, "GPRS connection lost, triggering reconnection");
            xEventGroupClearBits(aws_iot_event_group, GPRS_READY_BIT);
        } else {
            ESP_LOGW(TAG, "MQTT publish failed, may need to reconnect");
            xEventGroupClearBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
        }
    }

    return ret;
}

/**
 * Main AWS IoT task
 */
static void aws_iot_task(void *pvParameters) {
    ESP_LOGI(TAG, "AWS IoT SIM7600E task started");

    // Main connection loop with reconnection logic
    while (1) {
        // Check network connection
        if (!(xEventGroupGetBits(aws_iot_event_group) & NETWORK_READY_BIT)) {
            ESP_LOGW(TAG, "Network not ready, reinitializing...");
            if (init_network_and_gprs() != ESP_OK) {
                ESP_LOGE(TAG, "Network initialization failed, retrying in 30s");
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }
        }

        // Check GPRS connection
        if (!(xEventGroupGetBits(aws_iot_event_group) & GPRS_READY_BIT)) {
            ESP_LOGW(TAG, "GPRS not ready, reconnecting...");
            if (connect_to_gprs() != ESP_OK) {
                ESP_LOGE(TAG, "GPRS connection failed, retrying in 30s");
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }
        }

        // Check MQTT connection
        if (!(xEventGroupGetBits(aws_iot_event_group) &
              AWS_IOT_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "MQTT not connected, connecting...");
            if (connect_to_mqtt() != ESP_OK) {
                ESP_LOGE(TAG, "MQTT connection failed, retrying in 30s");
                vTaskDelay(pdMS_TO_TICKS(30000));
                continue;
            }

            // Subscribe to topics after successful connection
            if (subscribe_to_aws_topics() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to subscribe to topics, retrying");
                xEventGroupClearBits(aws_iot_event_group,
                                     AWS_IOT_CONNECTED_BIT);
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
        }

        // Connection established, main loop
        ESP_LOGI(TAG, "AWS IoT fully connected, entering main loop");

        TickType_t last_shadow_update = xTaskGetTickCount();
        const TickType_t shadow_update_interval =
            pdMS_TO_TICKS(30000); // 30 seconds

        while (xEventGroupGetBits(aws_iot_event_group) &
               (AWS_IOT_CONNECTED_BIT | NETWORK_READY_BIT | GPRS_READY_BIT)) {

            // Handle incoming messages
            handle_incoming_messages();

            // Publish shadow update periodically
            if ((xTaskGetTickCount() - last_shadow_update) >=
                shadow_update_interval) {
                if (publish_device_shadow() == ESP_OK) {
                    last_shadow_update = xTaskGetTickCount();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        ESP_LOGW(TAG, "Connection lost, will attempt to reconnect");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * Telemetry collection and publishing task
 */
static void telemetry_task(void *pvParameters) {
    ESP_LOGI(TAG, "Telemetry task started");

    // Wait for initial connection
    ESP_LOGI(TAG, "Waiting for AWS IoT to be ready...");
    xEventGroupWaitBits(aws_iot_event_group,
                        AWS_IOT_CONNECTED_BIT | AWS_IOT_SUBSCRIBED_BIT, false,
                        true, portMAX_DELAY);

    ESP_LOGI(TAG, "AWS IoT ready, starting telemetry collection");
    vTaskDelay(pdMS_TO_TICKS(5000)); // Additional delay for stability

    TickType_t last_telemetry = xTaskGetTickCount();
    const TickType_t telemetry_interval = pdMS_TO_TICKS(60000); // 1 minute

    while (1) {
        // Update telemetry data
        telemetry_data.heartbeat_counter++;

        // Get signal strength
        sim7600e_network_info_t net_info = {0};
        esp_err_t ret = sim7600e_gsm_get_network_info(&net_info);
        if (ret == ESP_OK) {
            telemetry_data.signal_strength = net_info.signal_strength;
        }

        // Read mocked digital inputs
        read_digital_inputs(telemetry_data.digital_inputs);

        // Check if it's time to publish telemetry
        if ((xTaskGetTickCount() - last_telemetry) >= telemetry_interval) {
            // Verify AWS IoT is connected
            if (xEventGroupGetBits(aws_iot_event_group) &
                (AWS_IOT_CONNECTED_BIT | AWS_IOT_SUBSCRIBED_BIT)) {
                if (publish_telemetry_data() == ESP_OK) {
                    last_telemetry = xTaskGetTickCount();
                }
            } else {
                ESP_LOGW(TAG, "AWS IoT not ready for telemetry publishing");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}

/**
 * Initialize AWS IoT client with SIM7600E
 */
esp_err_t aws_iot_sim7600e_init(void) {
    ESP_LOGI(TAG, "Initializing AWS IoT client with SIM7600E");

    // Initialize GPIO
    esp_err_t ret = init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO");
        return ret;
    }

    // Create event group
    aws_iot_event_group = xEventGroupCreate();
    if (!aws_iot_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // Setup device identity and topics
    setup_device_identity();
    setup_aws_iot_topics();

    // Initialize certificate manager
    ret = certificate_manager_sim7600e_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize certificate manager");
        return ret;
    }

    // Initialize SIM7600E
    sim7600e_config_t config = sim7600e_get_default_config();
    ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SIM7600E: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Power on the module
    ret = sim7600e_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on SIM7600E: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SIM7600E powered on, waiting for initialization...");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait for module initialization

    // Check modem and SIM
    ret = sim7600e_gsm_check_modem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Modem check failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sim7600e_gsm_check_sim();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM check failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize network and GPRS
    ret = init_network_and_gprs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Initialize device shadow
    ret = device_shadow_sim7600e_init(device_thing_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device shadow: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    // Create AWS IoT and telemetry tasks
    BaseType_t task_ret;

    task_ret = xTaskCreate(aws_iot_task, "aws_iot_task", 8192, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AWS IoT task");
        return ESP_FAIL;
    }

    task_ret =
        xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 4, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AWS IoT SIM7600E initialization completed");
    return ESP_OK;
}

#ifdef CONFIG_AWS_IOT_USE_SIM7600E
/**
 * @brief Application main entry point for SIM7600E mode
 */
void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 AWS IoT Client Starting (SIM7600E Mode)...");

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

    // Initialize GPIO
    ret = init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO, continuing anyway");
    }

    // Initialize the AWS IoT client with SIM7600E
    ESP_LOGI(TAG, "Initializing AWS IoT SIM7600E client...");
    ret = aws_iot_sim7600e_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AWS IoT SIM7600E client");
        return;
    }

    ESP_LOGI(TAG, "AWS IoT SIM7600E client initialization completed");

    // Monitor loop - tasks are running in background
    int count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds
        ESP_LOGI(TAG, "AWS IoT SIM7600E client running: %d", ++count);

        // Log network status periodically
        sim7600e_network_info_t info;
        if (sim7600e_gsm_get_network_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "Network: %s, Signal: %d dBm", info.operator_name,
                     info.signal_strength);
        }
    }
}
#endif // CONFIG_AWS_IOT_USE_SIM7600E