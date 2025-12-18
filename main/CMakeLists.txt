# Conditional source file compilation based on configuration
set(SOURCES)
set(CONNECTIVITY_REQUIRES)
set(MAIN_REQUIRES)
set(INCLUDE_DIRS)

# Set base requirements and include directories  
set(MAIN_REQUIRES mqtt esp_http_client nvs_flash json driver)
list(APPEND INCLUDE_DIRS ".")

# WiFi implementation
if(CONFIG_AWS_IOT_USE_WIFI)
    list(APPEND SOURCES 
        "aws_iot_client.c"
        "wifi_manager.c"
        "certificate_manager.c"
        "device_shadow.c"
    )
    list(APPEND CONNECTIVITY_REQUIRES esp_wifi)
    message(STATUS "Building with WiFi connectivity")
endif()

# SIM7600E implementation  
if(CONFIG_AWS_IOT_USE_SIM7600E)
    list(APPEND SOURCES 
        "aws_iot_sim7600e.c"
        "certificate_manager_sim7600e.c"
        "device_shadow_sim7600e.c"
    )
    list(APPEND CONNECTIVITY_REQUIRES sim7600e)
    list(APPEND MAIN_REQUIRES sim7600e)
    list(APPEND INCLUDE_DIRS "../components/sim7600e/include")
    message(STATUS "Building with SIM7600E cellular connectivity")
endif()

# Fallback if no configuration is detected - default to WiFi
if(NOT SOURCES)
    list(APPEND SOURCES 
        "aws_iot_client.c"
        "wifi_manager.c"
        "certificate_manager.c"
        "device_shadow.c"
    )
    list(APPEND CONNECTIVITY_REQUIRES esp_wifi)
    message(WARNING "No AWS IoT connectivity configuration detected, defaulting to WiFi")
endif()

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    REQUIRES ${MAIN_REQUIRES}
                    PRIV_REQUIRES ${CONNECTIVITY_REQUIRES}
                    EMBED_FILES "../certificates/aws_root_ca.pem"
                                "../certificates/device_cert.pem"
                                "../certificates/device_private_key.pem")