/**
 * @file certificate_manager.h
 * @brief AWS IoT certificate management header
 */

#ifndef CERTIFICATE_MANAGER_H
#define CERTIFICATE_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initialize certificate manager and load certificates
 * @return ESP_OK on success
 */
esp_err_t certificate_manager_init(void);

/**
 * @brief Get AWS IoT Root CA certificate
 * @return Pointer to root CA certificate string
 */
const char* certificate_manager_get_root_ca(void);

/**
 * @brief Get device client certificate
 * @return Pointer to client certificate string
 */
const char* certificate_manager_get_client_cert(void);

/**
 * @brief Get device private key
 * @return Pointer to private key string
 */
const char* certificate_manager_get_client_key(void);

/**
 * @brief Cleanup certificate manager and free memory
 */
void certificate_manager_cleanup(void);

#endif // CERTIFICATE_MANAGER_H