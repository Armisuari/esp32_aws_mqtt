/**
 * @file aws_iot_config.h
 * @brief AWS IoT configuration header
 *
 * All values come from Kconfig (menuconfig). Do NOT hardcode credentials here.
 * Defaults live in sdkconfig.defaults.
 */

#ifndef AWS_IOT_CONFIG_H
#define AWS_IOT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AWS IoT Core ---- */
#define AWS_IOT_MQTT_HOST           CONFIG_AWS_IOT_MQTT_HOST
#define AWS_IOT_MQTT_PORT           CONFIG_AWS_IOT_MQTT_PORT
#define AWS_IOT_DEVICE_THING_NAME   CONFIG_AWS_IOT_DEVICE_THING_NAME

/* ---- Certificate filenames ---- */
#define AWS_IOT_ROOT_CA_FILENAME    CONFIG_AWS_IOT_ROOT_CA_FILENAME
#define AWS_IOT_CERTIFICATE_FILENAME CONFIG_AWS_IOT_CERTIFICATE_FILENAME
#define AWS_IOT_PRIVATE_KEY_FILENAME CONFIG_AWS_IOT_PRIVATE_KEY_FILENAME

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_CONFIG_H */