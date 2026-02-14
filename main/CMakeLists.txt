# SIM7600E AWS IoT – source list
set(SOURCES
    "aws_iot_sim7600e.c"
    "certificate_manager_sim7600e.c"
    "device_shadow_sim7600e.c"
)

set(INCLUDE_DIRS "." "../components/sim7600e/include")

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    REQUIRES mqtt nvs_flash json driver sim7600e
                    EMBED_FILES "../certificates/aws_root_ca.pem"
                                "../certificates/device_cert.pem"
                                "../certificates/device_private_key.pem")