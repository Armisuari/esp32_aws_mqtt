#ifndef AWS_IOT_CONFIG_H
#define AWS_IOT_CONFIG_H

// AWS IoT Configuration
// These values are configured in sdkconfig.defaults
#define CONFIG_AWS_IOT_MQTT_HOST                                               \
    "a26g2r8rrxpe0j-ats.iot.ap-southeast-1.amazonaws.com"
#define CONFIG_AWS_IOT_MQTT_PORT 8883
#define CONFIG_AWS_IOT_DEVICE_THING_NAME "esp32-s3-device"

// WiFi Configuration
#define CONFIG_EXAMPLE_WIFI_SSID "Noovoleum_Office"
#define CONFIG_EXAMPLE_WIFI_PASSWORD "greenenergychampion"
#define CONFIG_EXAMPLE_WIFI_MAXIMUM_RETRY 5

// Certificate filenames
#define CONFIG_AWS_IOT_ROOT_CA_FILENAME "aws_root_ca.pem"
#define CONFIG_AWS_IOT_CERTIFICATE_FILENAME "device_cert.pem"
#define CONFIG_AWS_IOT_PRIVATE_KEY_FILENAME "device_private_key.pem"

#endif // AWS_IOT_CONFIG_H