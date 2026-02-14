/**
 * @file certificate_manager_sim7600e.c
 * @brief Certificate management for SIM7600E AWS IoT SSL connections
 *
 * Configures TLS 1.2 mutual authentication on the SIM7600E module using
 * certificates that were previously downloaded to the module's filesystem.
 * An NVS flag tracks whether the initial download has been performed.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "certificate_manager_sim7600e.h"
#include "sim7600e_gsm.h"

/* Extern linker symbols for embedded certificates (via EMBED_FILES) */
extern const uint8_t aws_root_ca_pem_start[]
    asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[]
    asm("_binary_aws_root_ca_pem_end");

extern const uint8_t device_cert_pem_start[]
    asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[]
    asm("_binary_device_cert_pem_end");

extern const uint8_t device_private_key_pem_start[]
    asm("_binary_device_private_key_pem_start");
extern const uint8_t device_private_key_pem_end[]
    asm("_binary_device_private_key_pem_end");

static const char *TAG = "CERT_MGR_SIM7600E";

/* NVS keys */
#define NVS_NAMESPACE           "certificates"
#define NVS_CERT_CONFIGURED     "cert_configured"

/* Certificate file names on the SIM7600E filesystem */
#define CERT_CA_ROOT            "aws_root_ca.pem"
#define CERT_DEVICE_CERT        "device_cert.pem"
#define CERT_DEVICE_PRIVATE_KEY "device_private_key.pem"

/* Forward declaration */
esp_err_t configure_certificate(void);

/* ---- NVS helpers -------------------------------------------------------- */

static bool are_certificates_configured(void) {
    nvs_handle_t nvs;
    uint8_t configured = 0;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t ret = nvs_get_u8(nvs, NVS_CERT_CONFIGURED, &configured);
    nvs_close(nvs);

    return (ret == ESP_OK && configured == 1);
}

static esp_err_t mark_certificates_configured(uint8_t configured) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) { return ret; }

    ret = nvs_set_u8(nvs, NVS_CERT_CONFIGURED, configured);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

/* ---- Public API --------------------------------------------------------- */

esp_err_t certificate_manager_sim7600e_init(void) {
    ESP_LOGI(TAG, "Initialising certificate manager for SIM7600E");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    return ESP_OK;
}

esp_err_t certificate_manager_sim7600e_configure_aws_iot(void) {
    char response[256];
    esp_err_t ret;

    ESP_LOGI(TAG, "Configuring SSL for AWS IoT");

    if (!are_certificates_configured()) {
        ESP_LOGE(TAG, "SSL not configured (certificates not yet downloaded)");
        return ESP_FAIL;
    }

    /* TLS 1.2 */
    ret = sim7600e_gsm_send_at_command(
        "AT+CSSLCFG=\"sslversion\",0,4\r\n",
        response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set SSL version: %s", response);
    }

    /* Mutual authentication */
    ret = sim7600e_gsm_send_at_command(
        "AT+CSSLCFG=\"authmode\",0,2\r\n",
        response, sizeof(response), 3000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set auth mode: %s", response);
    }

    /* CA root certificate */
    char cmd[64];
    snprintf(cmd, sizeof(cmd),
             "AT+CSSLCFG=\"cacert\",0,\"%s\"\r\n", CERT_CA_ROOT);
    ret = sim7600e_gsm_send_at_command(cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set CA root: %s", response);
    }

    /* Device certificate */
    snprintf(cmd, sizeof(cmd),
             "AT+CSSLCFG=\"clientcert\",0,\"%s\"\r\n", CERT_DEVICE_CERT);
    ret = sim7600e_gsm_send_at_command(cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device cert: %s", response);
    }

    /* Device private key */
    snprintf(cmd, sizeof(cmd),
             "AT+CSSLCFG=\"clientkey\",0,\"%s\"\r\n", CERT_DEVICE_PRIVATE_KEY);
    ret = sim7600e_gsm_send_at_command(cmd, response, sizeof(response), 5000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set device key: %s", response);
    }

    /* Debug: list certificates on module */
    ret = sim7600e_gsm_send_at_command(
        "AT+CCERTLIST\r\n", response, sizeof(response), 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Certificates in module:\n%s", response);
    }

    ESP_LOGI(TAG, "SSL configuration completed");
    return ESP_OK;
}

esp_err_t certificate_manager_sim7600e_clear_certificates(void) {
    ESP_LOGI(TAG, "Clearing SSL configuration flag");

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) { return ret; }

    nvs_erase_key(nvs, NVS_CERT_CONFIGURED);
    ret = nvs_commit(nvs);
    nvs_close(nvs);
    return ret;
}

/**
 * Download all three certificates to the SIM7600E module filesystem.
 */
esp_err_t configure_certificate(void) {
    ESP_LOGI(TAG, "Downloading certificates to SIM7600E module");

    if (download_certificates_to_module(CERT_CA_ROOT,
            aws_root_ca_pem_start, aws_root_ca_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download CA root certificate");
        return ESP_FAIL;
    }

    if (download_certificates_to_module(CERT_DEVICE_CERT,
            device_cert_pem_start, device_cert_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download device certificate");
        return ESP_FAIL;
    }

    if (download_certificates_to_module(CERT_DEVICE_PRIVATE_KEY,
            device_private_key_pem_start, device_private_key_pem_end) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download device private key");
        return ESP_FAIL;
    }

    return ESP_OK;
}