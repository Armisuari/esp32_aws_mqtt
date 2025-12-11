/**
 * @file certificate_manager.c
 * @brief AWS IoT certificate management for ESP32-S3
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "certificate_manager.h"

static const char *TAG = "CERT_MANAGER";

// Certificate storage - these will be populated from files
static char *root_ca_cert = NULL;
static char *client_cert = NULL;
static char *client_private_key = NULL;

// AWS IoT Root CA certificate (Amazon Root CA 1)
// This is the standard AWS IoT Root CA that doesn't change
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");

// Device certificate - will be embedded from certificates/device_cert.pem
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[] asm("_binary_device_cert_pem_end");

// Device private key - will be embedded from certificates/device_private_key.pem
extern const uint8_t device_private_key_pem_start[] asm("_binary_device_private_key_pem_start");
extern const uint8_t device_private_key_pem_end[] asm("_binary_device_private_key_pem_end");

esp_err_t certificate_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing certificate manager");
    
    // Load AWS Root CA
    size_t root_ca_len = aws_root_ca_pem_end - aws_root_ca_pem_start;
    root_ca_cert = malloc(root_ca_len + 1);
    if (root_ca_cert == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for root CA");
        return ESP_ERR_NO_MEM;
    }
    memcpy(root_ca_cert, aws_root_ca_pem_start, root_ca_len);
    root_ca_cert[root_ca_len] = '\0';
    ESP_LOGI(TAG, "Root CA loaded, size: %zu bytes", root_ca_len);
    
    // Load device certificate
    size_t client_cert_len = device_cert_pem_end - device_cert_pem_start;
    client_cert = malloc(client_cert_len + 1);
    if (client_cert == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for client certificate");
        return ESP_ERR_NO_MEM;
    }
    memcpy(client_cert, device_cert_pem_start, client_cert_len);
    client_cert[client_cert_len] = '\0';
    ESP_LOGI(TAG, "Client certificate loaded, size: %zu bytes", client_cert_len);
    
    // Load device private key
    size_t client_key_len = device_private_key_pem_end - device_private_key_pem_start;
    client_private_key = malloc(client_key_len + 1);
    if (client_private_key == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for client private key");
        return ESP_ERR_NO_MEM;
    }
    memcpy(client_private_key, device_private_key_pem_start, client_key_len);
    client_private_key[client_key_len] = '\0';
    ESP_LOGI(TAG, "Client private key loaded, size: %zu bytes", client_key_len);
    
    return ESP_OK;
}

const char* certificate_manager_get_root_ca(void)
{
    return root_ca_cert;
}

const char* certificate_manager_get_client_cert(void)
{
    return client_cert;
}

const char* certificate_manager_get_client_key(void)
{
    return client_private_key;
}

void certificate_manager_cleanup(void)
{
    if (root_ca_cert) {
        free(root_ca_cert);
        root_ca_cert = NULL;
    }
    if (client_cert) {
        free(client_cert);
        client_cert = NULL;
    }
    if (client_private_key) {
        free(client_private_key);
        client_private_key = NULL;
    }
}