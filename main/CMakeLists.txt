idf_component_register(SRCS "aws_iot_client.c"
                             "wifi_manager.c"
                             "certificate_manager.c"
                             "device_shadow.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi mqtt esp_http_client nvs_flash json
                    EMBED_FILES "../certificates/aws_root_ca.pem"
                                "../certificates/device_cert.pem"
                                "../certificates/device_private_key.pem")