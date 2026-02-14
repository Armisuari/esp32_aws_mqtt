/**
 * @file aws_iot_sim7600e.c
 * @brief AWS IoT Core MQTT client using SIM7600E cellular module
 *
 * Provides AWS IoT connectivity over 4G via the SIM7600E modem:
 *   - Cellular connection management with automatic reconnection
 *   - SSL/TLS MQTT connection to AWS IoT Core
 *   - Device shadow synchronisation
 *   - Telemetry publishing
 *   - Command reception
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

#include "certificate_manager_sim7600e.h"
#include "device_shadow_sim7600e.h"
#include "driver/gpio.h"
#include "sim7600e.h"
#include "sim7600e_gsm.h"

static const char *TAG = "AWS_IOT_SIM7600E";

/* ---------- Event bits --------------------------------------------------- */
static EventGroupHandle_t aws_iot_event_group;
static const int AWS_IOT_CONNECTED_BIT  = BIT0;
static const int AWS_IOT_SUBSCRIBED_BIT = BIT1;
static const int NETWORK_READY_BIT      = BIT2;
static const int GPRS_READY_BIT         = BIT3;

/* ---------- Named constants ---------------------------------------------- */
#define AWS_IOT_ENDPOINT        CONFIG_AWS_IOT_MQTT_HOST
#define AWS_IOT_PORT            CONFIG_AWS_IOT_MQTT_PORT
#define APN                     CONFIG_SIM7600E_APN

#define GPIO_RELAY              4

#define RETRY_DELAY_MS          30000
#define RECONNECT_DELAY_MS      5000
#define SHADOW_UPDATE_MS        30000
#define TELEMETRY_INTERVAL_MS   60000
#define TELEMETRY_POLL_MS       10000
#define MODULE_BOOT_DELAY_MS    10000

#define AWS_IOT_TASK_STACK      8192
#define AWS_IOT_TASK_PRIO       5
#define TELEMETRY_TASK_STACK    4096
#define TELEMETRY_TASK_PRIO     4

#define MQTT_QOS                1
#define MQTT_PUB_TIMEOUT_MS     60

#define NUM_DIGITAL_INPUTS      4

/* ---------- Device identity ---------------------------------------------- */
static char device_mac[13];
static char device_thing_name[64];
static char client_id[32];

/* ---------- MQTT topics -------------------------------------------------- */
static char shadow_update_topic[128];
static char shadow_get_topic[128];
static char shadow_delta_topic[128];
static char telemetry_topic[128];
static char command_topic[128];

/* ---------- Telemetry data ----------------------------------------------- */
typedef struct {
    int      signal_strength;
    uint32_t heartbeat_counter;
    bool     digital_inputs[NUM_DIGITAL_INPUTS];
    bool     relay_output;
} device_telemetry_t;

static device_telemetry_t telemetry_data = {0};

/* ---------- Forward declarations ----------------------------------------- */
static esp_err_t init_gpio(void);
static void      read_digital_inputs(bool *inputs);
static void      shadow_callback(const device_shadow_state_t *state);
static void      setup_device_identity(void);
static void      setup_aws_iot_topics(void);

static esp_err_t activate_pdp_and_open_network(void);
static esp_err_t init_network_and_gprs(void);
static bool      is_network_connected(void);
static bool      is_gprs_connected(void);
static esp_err_t connect_to_gprs(void);
static esp_err_t connect_to_mqtt(void);

static esp_err_t mqtt_publish(const char *topic, const char *payload);
static esp_err_t subscribe_to_aws_topics(void);
static esp_err_t publish_device_shadow(void);
static esp_err_t publish_telemetry_data(void);

static void aws_iot_task(void *pvParameters);
static void telemetry_task(void *pvParameters);
static void urc_handler_task(void *pvParameters);

/* ========================================================================= */
/*  URC Handler                                                              */
/* ========================================================================= */

/**
 * Task to handle unsolicited result codes from the modem,
 * specifically incoming MQTT messages for the shadow.
 */
static void urc_handler_task(void *pvParameters) {
    QueueHandle_t urc_queue = sim7600e_get_urc_queue();
    sim7600e_msg_t msg;
    char current_topic[128] = {0};

    ESP_LOGI(TAG, "URC handler task started");

    while (1) {
        if (xQueueReceive(urc_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "URC: %s", msg.data);

            if (strstr(msg.data, "+CMQTTRXTOPIC:")) {
                /* Next messages should be the topic then the payload */
                sim7600e_msg_t topic_msg, payload_header, payload_msg;
                
                /* Wait for topic string */
                if (xQueueReceive(urc_queue, &topic_msg, pdMS_TO_TICKS(1000))) {
                    strncpy(current_topic, topic_msg.data, sizeof(current_topic) - 1);
                    
                    /* Wait for payload header */
                    if (xQueueReceive(urc_queue, &payload_header, pdMS_TO_TICKS(1000))) {
                        
                        /* Wait for actual payload */
                        if (xQueueReceive(urc_queue, &payload_msg, pdMS_TO_TICKS(1000))) {
                            ESP_LOGI(TAG, "MQTT message on %s: %s", 
                                     current_topic, payload_msg.data);
                            
                            device_shadow_sim7600e_handle_message(current_topic, 
                                                                  payload_msg.data);
                        }
                    }
                }
            }
        }
    }
}

/* ========================================================================= */
/*  GPIO                                                                     */
/* ========================================================================= */

static esp_err_t init_gpio(void) {
    gpio_config_t output_config = {
        .pin_bit_mask  = (1ULL << GPIO_RELAY),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&output_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure relay GPIO");
        return ret;
    }

    gpio_set_level(GPIO_RELAY, 0);
    ESP_LOGI(TAG, "GPIO initialised (relay only, inputs mocked)");
    return ESP_OK;
}

/**
 * Mock digital input states for demonstration.
 */
static void read_digital_inputs(bool *inputs) {
    static uint32_t counter = 0;
    counter++;

    inputs[0] = (counter % 10) < 5;
    inputs[1] = (counter % 7) < 3;
    inputs[2] = (counter % 3) == 0;
    inputs[3] = (esp_timer_get_time() / 1000000) % 2;

    ESP_LOGD(TAG, "Mock inputs: D0=%d, D1=%d, D2=%d, D3=%d",
             inputs[0], inputs[1], inputs[2], inputs[3]);
}

/* ========================================================================= */
/*  Shadow callback                                                          */
/* ========================================================================= */

static void shadow_callback(const device_shadow_state_t *state) {
    ESP_LOGI(TAG, "Shadow state change: relay=%s",
             state->relay_output ? "ON" : "OFF");

    gpio_set_level(GPIO_RELAY, state->relay_output);
    telemetry_data.relay_output = state->relay_output;
}

/* ========================================================================= */
/*  Device identity & topics                                                 */
/* ========================================================================= */

static void setup_device_identity(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(device_mac, sizeof(device_mac),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(device_thing_name, sizeof(device_thing_name),
             "esp32-s3-device-%s", device_mac);

    snprintf(client_id, sizeof(client_id), "esp32s3_%s", device_mac);

    ESP_LOGI(TAG, "Device MAC: %s", device_mac);
    ESP_LOGI(TAG, "Thing Name: %s", device_thing_name);
    ESP_LOGI(TAG, "Client ID:  %s", client_id);
}

static void setup_aws_iot_topics(void) {
    snprintf(shadow_update_topic, sizeof(shadow_update_topic),
             "$aws/things/%s/shadow/update", device_thing_name);
    snprintf(shadow_get_topic, sizeof(shadow_get_topic),
             "$aws/things/%s/shadow/get", device_thing_name);
    snprintf(shadow_delta_topic, sizeof(shadow_delta_topic),
             "$aws/things/%s/shadow/update/delta", device_thing_name);
    snprintf(telemetry_topic, sizeof(telemetry_topic),
             "device/%s/telemetry", device_thing_name);
    snprintf(command_topic, sizeof(command_topic),
             "device/%s/commands", device_thing_name);

    ESP_LOGI(TAG, "Shadow Update: %s", shadow_update_topic);
    ESP_LOGI(TAG, "Shadow Delta:  %s", shadow_delta_topic);
    ESP_LOGI(TAG, "Telemetry:     %s", telemetry_topic);
    ESP_LOGI(TAG, "Command:       %s", command_topic);
}

/* ========================================================================= */
/*  Network helpers                                                          */
/* ========================================================================= */

static bool is_network_connected(void) {
    char response[256];
    if (sim7600e_gsm_send_at_command("AT+CREG?\r\n", response,
                                     sizeof(response), 3000) != ESP_OK) {
        return false;
    }
    return strstr(response, "+CREG: 0,1") != NULL ||
           strstr(response, "+CREG: 0,5") != NULL;
}

static bool is_gprs_connected(void) {
    char response[256];
    if (sim7600e_gsm_send_at_command("AT+CGATT?\r\n", response,
                                     sizeof(response), 3000) != ESP_OK) {
        return false;
    }
    return strstr(response, "+CGATT: 1") != NULL;
}

/**
 * Shared PDP-activation + network-open sequence used by both
 * init_network_and_gprs() and connect_to_gprs().
 */
static esp_err_t activate_pdp_and_open_network(void) {
    char response[512];
    char command[128];
    esp_err_t ret;

    /* Configure PDP context */
    snprintf(command, sizeof(command),
             "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", APN);
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PDP context");
        return ret;
    }

    /* Activate PDP context */
    ret = sim7600e_gsm_send_at_command("AT+CGACT=1,1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PDP activation warning, continuing");
    }

    /* Get PDP address */
    ret = sim7600e_gsm_send_at_command("AT+CGPADDR=1\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "PDP address: %s", response);
    }

    /* Open network */
    ret = sim7600e_gsm_send_at_command("AT+NETOPEN\r\n", response,
                                       sizeof(response), 5000);
    if (ret == ESP_OK || strstr(response, "already opened") != NULL) {
        ESP_LOGI(TAG, "Network opened");
    } else {
        ESP_LOGW(TAG, "Network open returned: %s", response);
    }

    /* Check network state */
    ret = sim7600e_gsm_send_at_command("AT+NETSTATE\r\n", response,
                                       sizeof(response), 500);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Network state: %s", response);
    }

    return ESP_OK;
}

/* ========================================================================= */
/*  Network & GPRS init / reconnect                                          */
/* ========================================================================= */

static esp_err_t init_network_and_gprs(void) {
    char response[512];
    esp_err_t ret;

    ESP_LOGI(TAG, "Initialising network and GPRS...");

    ret = sim7600e_gsm_send_at_command("AT+CFUN=1\r\n", response,
                                       sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set full functionality");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ret = sim7600e_gsm_send_at_command("AT+CPIN?\r\n", response,
                                       sizeof(response), 10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check SIM card");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Informational queries */
    if (sim7600e_gsm_send_at_command("AT+CSQ\r\n", response,
                                     sizeof(response), 1000) == ESP_OK) {
        ESP_LOGI(TAG, "Signal quality: %s", response);
    }
    if (sim7600e_gsm_send_at_command("AT+CREG?\r\n", response,
                                     sizeof(response), 1000) == ESP_OK) {
        ESP_LOGI(TAG, "Network registration: %s", response);
    }
    if (sim7600e_gsm_send_at_command("AT+COPS?\r\n", response,
                                     sizeof(response), 1000) == ESP_OK) {
        ESP_LOGI(TAG, "Operator: %s", response);
    }
    if (sim7600e_gsm_send_at_command("AT+CGATT?\r\n", response,
                                     sizeof(response), 1000) == ESP_OK) {
        ESP_LOGI(TAG, "GPRS attachment: %s", response);
    }
    if (sim7600e_gsm_send_at_command("AT+CPSI?\r\n", response,
                                     sizeof(response), 500) == ESP_OK) {
        ESP_LOGI(TAG, "System info: %s", response);
    }

    /* Set transparent mode */
    ret = sim7600e_gsm_send_at_command("AT+CCHMODE=1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set transparent mode");
    }

    /* Shared PDP + network open sequence */
    ret = activate_pdp_and_open_network();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Network and GPRS initialisation completed");
    xEventGroupSetBits(aws_iot_event_group,
                       NETWORK_READY_BIT | GPRS_READY_BIT);
    return ESP_OK;
}

static esp_err_t connect_to_gprs(void) {
    char response[256];
    esp_err_t ret;

    ESP_LOGI(TAG, "Connecting to GPRS...");

    ret = sim7600e_gsm_send_at_command("AT+CGATT=1\r\n", response,
                                       sizeof(response), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach to GPRS");
        return ret;
    }

    ret = activate_pdp_and_open_network();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "GPRS connection established");
    xEventGroupSetBits(aws_iot_event_group, GPRS_READY_BIT);
    return ESP_OK;
}

/* ========================================================================= */
/*  MQTT connect / subscribe / publish                                       */
/* ========================================================================= */

static esp_err_t connect_to_mqtt(void) {
    char response[512];
    char command[256];
    esp_err_t ret;

    ESP_LOGI(TAG, "Connecting to AWS IoT MQTT broker...");

    /* Tear down any previous session */
    sim7600e_gsm_send_at_command("AT+CMQTTDISC=0,60\r\n",  response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTREL=0\r\n",      response, sizeof(response), 2000);
    sim7600e_gsm_send_at_command("AT+CMQTTSTOP\r\n",       response, sizeof(response), 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Enable SSL */
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSSLCFG=0,1\r\n",
                                       response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable SSL for MQTT: %s", response);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Configure SSL certificates */
    ret = certificate_manager_sim7600e_configure_aws_iot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SSL certificates");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Start MQTT service */
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSTART\r\n",
                                       response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT service: %s", response);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Acquire client */
    snprintf(command, sizeof(command),
             "AT+CMQTTACCQ=0,\"%s\",1\r\n", client_id);
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire MQTT client: %s", response);
        return ret;
    }
    ESP_LOGI(TAG, "MQTT client acquired: %s", client_id);
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Connect to broker */
    snprintf(command, sizeof(command),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1\r\n",
             AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    ESP_LOGI(TAG, "Connecting to %s:%d", AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 30000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to AWS IoT: %s", response);
        return ret;
    }

    if (strstr(response, "+CMQTTCONNECT: 0,0") != NULL ||
        strstr(response, "OK") != NULL) {
        ESP_LOGI(TAG, "Connected to AWS IoT Core");
        xEventGroupSetBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "AWS IoT connection failed: %s", response);
    return ESP_FAIL;
}

static esp_err_t subscribe_to_aws_topics(void) {
    ESP_LOGI(TAG, "Subscribing to AWS IoT topics...");
    esp_err_t ret;

    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Subscribing to: %s", shadow_delta_topic);
    ret = device_shadow_sim7600e_subscribe_delta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to shadow delta topics");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Subscribing to: %s", command_topic);
    ret = sim7600e_gsm_mqtt_subscribe(command_topic, MQTT_QOS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to command topic");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Subscribed to all AWS IoT topics");
    xEventGroupSetBits(aws_iot_event_group, AWS_IOT_SUBSCRIBED_BIT);
    return ESP_OK;
}

/**
 * Publish an MQTT message via the SIM7600E AT command 3-step sequence:
 *   1. AT+CMQTTTOPIC   – set topic
 *   2. AT+CMQTTPAYLOAD – set payload
 *   3. AT+CMQTTPUB     – publish
 */
static esp_err_t mqtt_publish(const char *topic, const char *payload) {
    char response[256];
    char command[128];
    esp_err_t ret;

    /* 1. Topic */
    snprintf(command, sizeof(command),
             "AT+CMQTTTOPIC=0,%d\r\n", (int)strlen(topic));
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 3000);
    if (ret != ESP_OK) { return ret; }

    char topic_buf[256];
    snprintf(topic_buf, sizeof(topic_buf), "%s\x1A", topic);
    ret = sim7600e_gsm_send_at_command(topic_buf, response,
                                       sizeof(response), 3000);
    if (ret != ESP_OK) { return ret; }

    /* 2. Payload */
    snprintf(command, sizeof(command),
             "AT+CMQTTPAYLOAD=0,%d\r\n", (int)strlen(payload));
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 3000);
    if (ret != ESP_OK) { return ret; }

    char payload_buf[512];
    snprintf(payload_buf, sizeof(payload_buf), "%s\x1A", payload);
    ret = sim7600e_gsm_send_at_command(payload_buf, response,
                                       sizeof(response), 3000);
    if (ret != ESP_OK) { return ret; }

    /* 3. Publish */
    snprintf(command, sizeof(command),
             "AT+CMQTTPUB=0,%d,%d\r\n", MQTT_QOS, MQTT_PUB_TIMEOUT_MS);
    ret = sim7600e_gsm_send_at_command(command, response,
                                       sizeof(response), 10000);
    return ret;
}

/* ========================================================================= */
/*  Shadow & telemetry publishing                                            */
/* ========================================================================= */

static esp_err_t publish_device_shadow(void) {
    device_shadow_state_t shadow_state = {
        .signal_strength = telemetry_data.signal_strength,
        .heartbeat       = telemetry_data.heartbeat_counter,
        .relay_output    = telemetry_data.relay_output,
        .temperature     = 25, /* TODO: read from actual sensor */
        .humidity        = 60, /* TODO: read from actual sensor */
    };

    strncpy(shadow_state.mac_address, device_mac,
            sizeof(shadow_state.mac_address) - 1);
    memcpy(shadow_state.digital_inputs, telemetry_data.digital_inputs,
           sizeof(shadow_state.digital_inputs));

    esp_err_t ret = device_shadow_sim7600e_update_reported(&shadow_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update shadow reported state");
        return ret;
    }

    ret = device_shadow_sim7600e_publish_update();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Device shadow published");
    } else {
        ESP_LOGE(TAG, "Failed to publish device shadow");
    }
    return ret;
}

static esp_err_t publish_telemetry_data(void) {
    /* Build JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", device_thing_name);
    cJSON_AddStringToObject(root, "mac_address", device_mac);
    cJSON_AddNumberToObject(root, "timestamp",
                            esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "signal_strength",
                            telemetry_data.signal_strength);
    cJSON_AddNumberToObject(root, "heartbeat",
                            telemetry_data.heartbeat_counter);

    cJSON *sensors = cJSON_CreateObject();
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
        char name[4];
        snprintf(name, sizeof(name), "D%d", i);
        cJSON_AddBoolToObject(sensors, name,
                              telemetry_data.digital_inputs[i]);
    }
    cJSON_AddItemToObject(root, "sensors", sensors);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) {
        ESP_LOGE(TAG, "Failed to create telemetry JSON");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Publishing telemetry: %s", json);

    esp_err_t ret = mqtt_publish(telemetry_topic, json);
    free(json);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Telemetry published");
    } else {
        ESP_LOGE(TAG, "Failed to publish telemetry");

        /* Diagnose which layer broke */
        if (!is_network_connected()) {
            ESP_LOGW(TAG, "Network lost, triggering reconnection");
            xEventGroupClearBits(aws_iot_event_group, NETWORK_READY_BIT);
        } else if (!is_gprs_connected()) {
            ESP_LOGW(TAG, "GPRS lost, triggering reconnection");
            xEventGroupClearBits(aws_iot_event_group, GPRS_READY_BIT);
        } else {
            ESP_LOGW(TAG, "MQTT publish failed, may need reconnect");
            xEventGroupClearBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
        }
    }
    return ret;
}

/* ========================================================================= */
/*  Tasks                                                                    */
/* ========================================================================= */

static void aws_iot_task(void *pvParameters) {
    ESP_LOGI(TAG, "AWS IoT task started");

    while (1) {
        /* --- Network layer --- */
        if (!(xEventGroupGetBits(aws_iot_event_group) & NETWORK_READY_BIT)) {
            ESP_LOGW(TAG, "Network not ready, reinitialising...");
            if (init_network_and_gprs() != ESP_OK) {
                ESP_LOGE(TAG, "Network init failed, retrying in %d s",
                         RETRY_DELAY_MS / 1000);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                continue;
            }
        }

        /* --- GPRS layer --- */
        if (!(xEventGroupGetBits(aws_iot_event_group) & GPRS_READY_BIT)) {
            ESP_LOGW(TAG, "GPRS not ready, reconnecting...");
            if (connect_to_gprs() != ESP_OK) {
                ESP_LOGE(TAG, "GPRS failed, retrying in %d s",
                         RETRY_DELAY_MS / 1000);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                continue;
            }
        }

        /* --- MQTT layer --- */
        if (!(xEventGroupGetBits(aws_iot_event_group) & AWS_IOT_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "MQTT not connected, connecting...");
            if (connect_to_mqtt() != ESP_OK) {
                ESP_LOGE(TAG, "MQTT failed, retrying in %d s",
                         RETRY_DELAY_MS / 1000);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                continue;
            }
            if (subscribe_to_aws_topics() != ESP_OK) {
                ESP_LOGE(TAG, "Subscribe failed, retrying");
                xEventGroupClearBits(aws_iot_event_group, AWS_IOT_CONNECTED_BIT);
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
        }

        /* --- Main connected loop --- */
        ESP_LOGI(TAG, "AWS IoT fully connected");

        TickType_t last_shadow = xTaskGetTickCount();
        const TickType_t shadow_interval = pdMS_TO_TICKS(SHADOW_UPDATE_MS);

        while (xEventGroupGetBits(aws_iot_event_group) &
               (AWS_IOT_CONNECTED_BIT | NETWORK_READY_BIT | GPRS_READY_BIT)) {

            /* Yield for background UART handler */
            vTaskDelay(pdMS_TO_TICKS(10));

            if ((xTaskGetTickCount() - last_shadow) >= shadow_interval) {
                if (publish_device_shadow() == ESP_OK) {
                    last_shadow = xTaskGetTickCount();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        ESP_LOGW(TAG, "Connection lost, reconnecting in %d s",
                 RECONNECT_DELAY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
    }
}

static void telemetry_task(void *pvParameters) {
    ESP_LOGI(TAG, "Telemetry task started, waiting for AWS IoT...");

    xEventGroupWaitBits(aws_iot_event_group,
                        AWS_IOT_CONNECTED_BIT | AWS_IOT_SUBSCRIBED_BIT,
                        false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "AWS IoT ready, starting telemetry collection");
    vTaskDelay(pdMS_TO_TICKS(5000));

    TickType_t last_publish = xTaskGetTickCount();
    const TickType_t publish_interval = pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS);

    while (1) {
        telemetry_data.heartbeat_counter++;

        sim7600e_network_info_t net_info = {0};
        if (sim7600e_gsm_get_network_info(&net_info) == ESP_OK) {
            telemetry_data.signal_strength = net_info.signal_strength;
        }

        read_digital_inputs(telemetry_data.digital_inputs);

        if ((xTaskGetTickCount() - last_publish) >= publish_interval) {
            if (xEventGroupGetBits(aws_iot_event_group) &
                (AWS_IOT_CONNECTED_BIT | AWS_IOT_SUBSCRIBED_BIT)) {
                if (publish_telemetry_data() == ESP_OK) {
                    last_publish = xTaskGetTickCount();
                }
            } else {
                ESP_LOGW(TAG, "AWS IoT not ready for telemetry");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_POLL_MS));
    }
}

/* ========================================================================= */
/*  Public init                                                              */
/* ========================================================================= */

esp_err_t aws_iot_sim7600e_init(void) {
    ESP_LOGI(TAG, "Initialising AWS IoT client with SIM7600E");

    esp_err_t ret = init_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise GPIO");
        return ret;
    }

    aws_iot_event_group = xEventGroupCreate();
    if (!aws_iot_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    setup_device_identity();
    setup_aws_iot_topics();

    ret = certificate_manager_sim7600e_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Certificate manager init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    sim7600e_config_t config = sim7600e_get_default_config();
    ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM7600E init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sim7600e_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SIM7600E power-on failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SIM7600E powered on, waiting for module boot...");
    vTaskDelay(pdMS_TO_TICKS(MODULE_BOOT_DELAY_MS));

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

    ret = init_network_and_gprs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = device_shadow_sim7600e_init(device_thing_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device shadow init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    device_shadow_sim7600e_set_callback(shadow_callback);

    BaseType_t task_ret;
    task_ret = xTaskCreate(urc_handler_task, "urc_handler_task",
                           4096, NULL, 6, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create URC handler task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(aws_iot_task, "aws_iot_task",
                           AWS_IOT_TASK_STACK, NULL, AWS_IOT_TASK_PRIO, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AWS IoT task");
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(telemetry_task, "telemetry_task",
                           TELEMETRY_TASK_STACK, NULL, TELEMETRY_TASK_PRIO, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AWS IoT SIM7600E initialisation completed");
    return ESP_OK;
}

/* ========================================================================= */
/*  app_main                                                                 */
/* ========================================================================= */

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 AWS IoT Client Starting (SIM7600E)...");

    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize AWS IoT SIM7600E
    ret = aws_iot_sim7600e_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AWS IoT SIM7600E init failed");
        return;
    }

    ESP_LOGI(TAG, "AWS IoT SIM7600E client running");

    int count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        ESP_LOGI(TAG, "Heartbeat #%d", ++count);

        sim7600e_network_info_t info;
        if (sim7600e_gsm_get_network_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "Network: %s, Signal: %d dBm",
                     info.operator_name, info.signal_strength);
        }
    }
}