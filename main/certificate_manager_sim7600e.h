/**
 * @file certificate_manager_sim7600e.h
 * @brief Certificate management header for SIM7600E AWS IoT SSL connections
 */

#ifndef CERTIFICATE_MANAGER_SIM7600E_H
#define CERTIFICATE_MANAGER_SIM7600E_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize certificate manager for SIM7600E
 *
 * @return esp_err_t
 */
esp_err_t certificate_manager_sim7600e_init(void);

/**
 * @brief Configure AWS IoT certificates for SIM7600E
 *
 * This function loads certificates from NVS and uploads them to the SIM7600E
 * module
 *
 * @return esp_err_t
 */
esp_err_t certificate_manager_sim7600e_configure_aws_iot(void);

/**
 * @brief Clear stored certificates from NVS
 *
 * @return esp_err_t
 */
esp_err_t certificate_manager_sim7600e_clear_certificates(void);

#ifdef __cplusplus
}
#endif

#endif // CERTIFICATE_MANAGER_SIM7600E_H