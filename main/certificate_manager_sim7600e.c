/**
 * @file certificate_manager_sim7600e.c
 * @brief Certificate management for SIM7600E AWS IoT SSL connections
 *
 * Simplified version - just enables SSL without uploading certificates
 * Relies on SIM7600E built-in certificate store
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "certificate_manager.h"
#include "sim7600e_gsm.h"
#include "esp_task_wdt.h"

// Extern linker symbols for embedded certificates (from EMBED_FILES)
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");

// Device certificate - will be embedded from certificates/device_cert.pem
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[] asm("_binary_device_cert_pem_end");

// Device private key - will be embedded from certificates/device_private_key.pem
extern const uint8_t device_private_key_pem_start[] asm("_binary_device_private_key_pem_start");
extern const uint8_t device_private_key_pem_end[] asm("_binary_device_private_key_pem_end");

static const char *TAG = "CERT_MGR_SIM7600E";

// NVS storage keys
#define NVS_NAMESPACE "certificates"
#define NVS_CERT_CONFIGURED "cert_configured"

#define CERT_CA_ROOT "aws_root_ca.pem"
#define CERT_DEVICE_CERT "device_cert.pem"
#define CERT_DEVICE_PRIVATE_KEY "device_private_key.pem"

esp_err_t configure_certificate(void);

/**
 * @brief Check if SSL is already configured
 */
static bool are_certificates_configured(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    uint8_t configured = 0;

    // Open NVS namespace
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return false;
    }

    // Read configured flag
    ret = nvs_get_u8(nvs_handle, NVS_CERT_CONFIGURED, &configured);
    nvs_close(nvs_handle);

    return (ret == ESP_OK && configured == 1);
}

/**
 * @brief Mark SSL as configured
 */
static esp_err_t mark_certificates_configured(uint8_t configured) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

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
    esp_err_t ret = ESP_FAIL;
    char response[256];

    ESP_LOGI(TAG, "Configuring SSL for AWS IoT (using built-in certificates)");

    // mark_certificates_configured(1);

    // Check if already configured
    if (!are_certificates_configured()) {
        ESP_LOGE(TAG, "SSL not configured");
        return ESP_FAIL;
    }

    // step 1: Configure SSL version
    ESP_LOGI(TAG, "Configuring SSL version to TLS 1.2");
    ret = sim7600e_gsm_send_at_command("AT+CSSLCFG=\"sslversion\",0,4\r\n", response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set SSL version: %s", response);
    }

    // step 2: Configure authentication mode to mutual authentication
    ESP_LOGI(TAG, "Setting authentication mode to mutual authentication");
    ret = sim7600e_gsm_send_at_command("AT+CSSLCFG=\"authmode\",0,2\r\n", response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set authentication mode: %s", response);
    }

    // step 3 and step 4 for ignore local and negotiation time were skipped

    // step 5: Configure the server root CA certificate
    ESP_LOGI(TAG, "Configuring server root CA certificate");
    char ca_cmd[64];
    snprintf(ca_cmd, sizeof(ca_cmd), "AT+CSSLCFG=\"cacert\",0,\"%s\"\r\n", CERT_CA_ROOT);
    ret = sim7600e_gsm_send_at_command(ca_cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set CA root certificate: %s", response);
    }

    // step 6: Configure the device certificate
    ESP_LOGI(TAG, "Configuring device certificate");
    char cert_cmd[64];
    snprintf(cert_cmd, sizeof(cert_cmd), "AT+CSSLCFG=\"clientcert\",0,\"%s\"\r\n", CERT_DEVICE_CERT);
    ret = sim7600e_gsm_send_at_command(cert_cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device certificate: %s", response);
    }

    // step 7: Configure the device private key
    ESP_LOGI(TAG, "Configuring device private key");
    char key_cmd[64];
    snprintf(key_cmd, sizeof(key_cmd), "AT+CSSLCFG=\"clientkey\",0,\"%s\"\r\n", CERT_DEVICE_PRIVATE_KEY);
    ret = sim7600e_gsm_send_at_command(key_cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device private key: %s", response);
    }

    // // step 8: Download certificates to the module
    // ESP_LOGI(TAG, "Downloading certificates to the module");
    // if (configure_certificate() != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to download certificates to the module");
    //     return ESP_FAIL;
    // }

    // // step 9: Delete the certificates inside the module (for debugging)
    // ESP_LOGW(TAG, "Deleting existing certificates inside the module");
    // char delete_cmd[64];
    // snprintf(delete_cmd, sizeof(delete_cmd), "AT+CCERTDEL=0,\"%s\"\r\n", CERT_CA_ROOT);
    // ret = sim7600e_gsm_send_at_command(delete_cmd, response, sizeof(response), 5000);
    // // ret = sim7600e_gsm_send_at_command("AT+CCERTDELE=\"cacert.pem\"\r\n", response, sizeof(response), 5000);
    // if (ret != ESP_OK) {
    //     ESP_LOGW(TAG, "Failed to delete CA root certificate: %s", response);
    // }

    // step 10: List the certificates inside the module (for debugging)
    ESP_LOGW(TAG, "Listing certificates inside the module");
    ret = sim7600e_gsm_send_at_command("AT+CCERTLIST\r\n", response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to list certificates: %s", response);
    } else {
        ESP_LOGI(TAG, "Certificates in module:\n%s", response);
    }

    // // Link SSL context 0 to MQTT client 0
    // ESP_LOGI(TAG, "Linking SSL context to MQTT client");
    // ret = sim7600e_gsm_send_at_command("AT+CMQTTSSLCFG=0,0\r\n",
    //                                    response, sizeof(response), 3000);
    // if (ret != ESP_OK) {
    //     ESP_LOGW(TAG, "Failed to link SSL context: %s", response);
    // }

    // Mark as configured
    // mark_certificates_configured(1);

    ESP_LOGI(TAG, "SSL configuration for AWS IoT completed");
    
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

/**
 * @brief Download certificates to SIM7600E module
 */
esp_err_t configure_certificate(void) {
    ESP_LOGI(TAG, "Configuring certificate in SIM7600E module");

    // Download CA root certificate
    if(download_certificates_to_module(CERT_CA_ROOT, aws_root_ca_pem_start, aws_root_ca_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download CA root certificate");
        return ESP_FAIL;
    }

    // Download device certificate
    if(download_certificates_to_module(CERT_DEVICE_CERT, device_cert_pem_start, device_cert_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download device certificate");
        return ESP_FAIL;
    }

    // Download device private key
    if(download_certificates_to_module(CERT_DEVICE_PRIVATE_KEY, device_private_key_pem_start, device_private_key_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download device private key");
        return ESP_FAIL;
    }

    return ESP_OK;
}