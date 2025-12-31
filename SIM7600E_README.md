# AWS IoT SIM7600E Implementation

This implementation provides AWS IoT connectivity using the **SIM7600E cellular module** instead of WiFi. This is ideal for remote deployments, mobile applications, or areas with limited or no WiFi coverage.

> [!NOTE]
> For WiFi-based connectivity, see the main [README.md](README.md). This document focuses on the cellular (SIM7600E) implementation.

## Overview

The SIM7600E implementation provides all the same AWS IoT features as the WiFi version:
- Secure MQTT connection to AWS IoT Core with SSL/TLS
- Device Shadow synchronization
- Telemetry data publishing
- Remote command reception
- Certificate-based authentication

## Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   ESP32-S3      │    │    SIM7600E     │    │   AWS IoT Core  │
│                 │    │   Cellular      │    │                 │
│  Application    │◄──►│    Module       │◄──►│   MQTT Broker   │
│                 │    │                 │    │                 │
│  - Shadow Mgmt  │    │  - AT Commands  │    │  - Device Shadow│
│  - Telemetry    │    │  - SSL/TLS      │    │  - Message Broker│
│  - Commands     │    │  - 4G/LTE       │    │  - Authentication│
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Key Components

### 1. Main AWS IoT Client (`aws_iot_sim7600e.c`)
- Main application entry point
- Manages SIM7600E initialization and cellular connection
- Handles AWS IoT MQTT connection with SSL
- Coordinates telemetry publishing and shadow updates

### 2. Certificate Manager (`certificate_manager_sim7600e.c/.h`)
- Manages AWS IoT certificates for SSL authentication
- Stores certificates in NVS (Non-Volatile Storage)
- Uploads certificates to SIM7600E module
- Configures SSL context for AWS IoT connections

### 3. Device Shadow (`device_shadow_sim7600e.c/.h`)
- Implements AWS IoT Device Shadow functionality
- Handles shadow state updates (reported/desired)
- Processes shadow delta messages for remote control
- Thread-safe shadow state management

### 4. Configuration (`aws_iot_config_sim7600e.h`)
- SIM7600E-specific configuration parameters
- GPIO pin mappings for digital inputs/outputs
- Network and MQTT settings
- Timing and interval configurations

### 5. Example Implementation (`aws_iot_sim7600e_example.c/.h`)
- Demonstrates usage of the SIM7600E AWS IoT client
- Includes test functions for validation
- GPIO handling for digital inputs and relay output
- Sensor simulation task

## Features

### Cellular Connectivity
- 4G LTE connectivity via SIM7600E module
- Automatic network registration
- Configurable APN settings
- Network signal monitoring

### AWS IoT Integration
- Secure MQTT over TLS (port 8883)
- X.509 certificate authentication
- Device Shadow synchronization
- Custom telemetry topics
- Command reception via MQTT

### Device Shadow
- Real-time state synchronization with AWS IoT
- Reported state: device status, sensor readings, connectivity info
- Desired state: remote control commands (relay output)
- Delta processing for state changes

### Telemetry Data
- Signal strength monitoring
- Digital input states (4 channels)
- Heartbeat counter
- Temperature and humidity (simulated/configurable)
- Configurable publishing intervals

## Usage

### 1. Hardware Setup
Connect the SIM7600E module to ESP32-S3:
```
SIM7600E  <->  ESP32-S3
TX        <->  GPIO1 (RX)
RX        <->  GPIO2 (TX)
PWRKEY    <->  GPIO41
GND       <->  GND
VCC       <->  5V
```

### 2. Build for SIM7600E Mode

#### Windows
```powershell
# Build for SIM7600E cellular mode
.\\build.bat --mode sim7600e

# Flash and monitor
idf.py flash monitor
```

#### Linux/macOS
```bash
# Build for SIM7600E cellular mode
./build.sh --mode sim7600e

# Flash and monitor
idf.py flash monitor
```

### 3. Certificate Setup
Before using the implementation, you need to provision AWS IoT certificates:

```c
// Store certificates during device provisioning
certificate_manager_sim7600e_store_certificates(
    aws_root_ca_pem,     // AWS Root CA
    device_cert_pem,     // Device certificate
    device_private_key_pem // Device private key
);
```

### 3. Basic Usage
```c
#include \"aws_iot_sim7600e.h\"

void app_main(void) {
    // Initialize NVS
    nvs_flash_init();
    
    // Initialize and start AWS IoT client
    esp_err_t ret = aws_iot_sim7600e_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, \"AWS IoT initialization failed\");
        return;
    }
    
    // Main application loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

### 4. Shadow Callback
Handle desired state changes from AWS IoT:

```c
void shadow_callback(const device_shadow_state_t *state) {
    // Update relay based on desired state
    gpio_set_level(RELAY_GPIO, state->relay_output);
    
    ESP_LOGI(TAG, \"Relay state changed: %s\", 
             state->relay_output ? \"ON\" : \"OFF\");
}

// Register callback
device_shadow_sim7600e_set_callback(shadow_callback);
```

## Configuration

### Network Settings
- APN configuration in `aws_iot_config_sim7600e.h`
- Default: `\"internet\"` (modify for your carrier)
- Network registration timeout: 60 seconds
- Connection retry logic included

### MQTT Topics
The implementation uses these AWS IoT topics:
- Device Shadow: `$aws/things/{thing-name}/shadow/update`
- Shadow Delta: `$aws/things/{thing-name}/shadow/update/delta`
- Telemetry: `device/{thing-name}/telemetry`
- Commands: `device/{thing-name}/commands`

### GPIO Configuration
Digital inputs and relay output can be configured in the header file:
```c
#define SIM7600E_GPIO_D0 36      // Digital input 0
#define SIM7600E_GPIO_D1 35      // Digital input 1
#define SIM7600E_GPIO_D2 32      // Digital input 2
#define SIM7600E_GPIO_D3 33      // Digital input 3
#define SIM7600E_GPIO_RELAY 4    // Relay output
```

## Testing

Run the included tests to validate functionality:

```c
#include \"aws_iot_sim7600e_example.h\"

// Run comprehensive tests
esp_err_t ret = aws_iot_sim7600e_run_tests();
if (ret == ESP_OK) {
    ESP_LOGI(TAG, \"All tests passed!\");
}
```

## Troubleshooting

### Common Issues

1. **SIM Card not detected**
   - Verify SIM card is properly inserted
   - Check SIM card activation status
   - Verify PIN code (if required)

2. **Network registration failed**
   - Check cellular coverage in your area
   - Verify APN settings for your carrier
   - Check SIM card plan and data allowance

3. **AWS IoT connection failed**
   - Verify certificates are correctly stored
   - Check AWS IoT endpoint URL
   - Verify clock synchronization (important for TLS)

4. **Certificate upload failed**
   - Ensure certificates are in proper PEM format
   - Check certificate file sizes
   - Verify NVS partition has sufficient space

### Debugging

Enable debug logging for detailed troubleshooting:

```c
// In menuconfig or sdkconfig
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y

// Or set log level in code
esp_log_level_set(\"AWS_IOT_SIM7600E\", ESP_LOG_DEBUG);
esp_log_level_set(\"SHADOW_SIM7600E\", ESP_LOG_DEBUG);
esp_log_level_set(\"CERT_MGR_SIM7600E\", ESP_LOG_DEBUG);
```

## Integration with Existing Code

The SIM7600E implementation can be used alongside the WiFi implementation:

1. Include both implementations in your build
2. Choose connectivity method based on availability
3. Use conditional compilation for different deployment scenarios

```c
#ifdef CONFIG_USE_CELLULAR
    // Use SIM7600E implementation
    ret = aws_iot_sim7600e_init();
#else
    // Use WiFi implementation
    ret = aws_iot_wifi_init();
#endif
```

## Performance Considerations

- **Power consumption**: Cellular connectivity uses more power than WiFi
- **Data usage**: Monitor MQTT message sizes to control data costs
- **Latency**: Cellular networks may have higher latency than WiFi
- **Connection stability**: Implement reconnection logic for network outages

## Future Enhancements

- Over-the-Air (OTA) firmware updates via cellular
- GPS integration for location-based services
- SMS fallback for critical commands
- Power management optimization for battery operation
- Multi-carrier SIM support