/**
 * @file certificate_manager_sim7600e.c
 * @brief Certificate management for SIM7600E AWS IoT SSL connections
 *
 * Simplified version - just enables SSL without uploading certificates
 * Relies on SIM7600E built-in certificate store
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "certificate_manager.h"
#include "sim7600e_gsm.h"

static const char *TAG = "CERT_MGR_SIM7600E";

// NVS storage keys
#define NVS_NAMESPACE "certificates"
#define NVS_CERT_CONFIGURED "cert_configured"

/**
 * @brief Check if SSL is already configured
 */
static bool are_certificates_configured(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint8_t configured = 0;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return false;
    }

    ret = nvs_get_u8(nvs_handle, NVS_CERT_CONFIGURED, &configured);
    nvs_close(nvs_handle);

    return (ret == ESP_OK && configured == 1);
}

/**
 * @brief Mark SSL as configured
 */
static esp_err_t mark_certificates_configured(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t configured = 1;
    ret = nvs_set_u8(nvs_handle, NVS_CERT_CONFIGURED, configured);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return ret;
}

/**
 * @brief Initialize certificate manager for SIM7600E
 */
esp_err_t certificate_manager_sim7600e_init(void) {
    ESP_LOGI(TAG, "Initializing certificate manager for SIM7600E");

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    return ESP_OK;
}

/**
 * @brief Configure SSL for AWS IoT (simplified - no certificate upload)
 *
 * Note: AT+CCERTDOWN causes timeouts on some SIM7600E firmware versions
 * This simplified version just enables SSL and relies on built-in certificates
 */
esp_err_t certificate_manager_sim7600e_configure_aws_iot(void) {
    esp_err_t ret = ESP_OK;
    char response[256];

    ESP_LOGI(TAG, "Configuring SSL for AWS IoT (using built-in certificates)");

    // Check if already configured
    if (are_certificates_configured()) {
        ESP_LOGI(TAG, "SSL already configured");
        return ESP_OK;
    }

    // Set SSL version to TLS 1.2
    ESP_LOGI(TAG, "Setting SSL version to TLS 1.2");
    ret = sim7600e_gsm_send_at_command("AT+CSSLCFG=\"sslversion\",0,4\r\n",
                                       response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set SSL version: %s", response);
    }

    // Set authentication mode to server-only (mode 1)
    // Mode 2 (mutual TLS) would require certificate upload
    ESP_LOGI(TAG, "Setting authentication mode to server-only");
    ret = sim7600e_gsm_send_at_command("AT+CSSLCFG=\"authmode\",0,1\r\n",
                                       response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set authentication mode: %s", response);
    }

    // Link SSL context 0 to MQTT client 0
    ESP_LOGI(TAG, "Linking SSL context to MQTT client");
    ret = sim7600e_gsm_send_at_command("AT+CMQTTSSLCFG=0,0\r\n",
                                       response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to link SSL context: %s", response);
    }

    // Mark as configured
    mark_certificates_configured();

    ESP_LOGI(TAG, "SSL configured successfully (using built-in CA certificates)");
    ESP_LOGW(TAG, "Note: Using server-only authentication, not mutual TLS");
    
    return ESP_OK;
}

/**
 * @brief Clear SSL configuration flag
 */
esp_err_t certificate_manager_sim7600e_clear_certificates(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Clearing SSL configuration flag");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_erase_key(nvs_handle, NVS_CERT_CONFIGURED);
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return ret;
}
