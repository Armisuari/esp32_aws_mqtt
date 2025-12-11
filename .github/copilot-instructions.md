# ESP32-S3 AWS IoT MQTT TLS Project - Copilot Instructions

## Project Overview
ESP-IDF project for ESP32-S3 that connects to AWS IoT Core using secure MQTT over TLS. Implements device shadows, telemetry publishing, and command receiving.

## Key Components
- **Platform**: ESP32-S3 with ESP-IDF v5.3+
- **Connectivity**: WiFi with AWS IoT Core MQTT TLS (port 8883)
- **Security**: X.509 certificates for AWS IoT device authentication
- **Features**: Device shadows, telemetry, remote commands
- **Build**: Cross-platform automation (Windows/Linux)

## Project Structure
```
esp32_aws_mqtt/
├── main/
│   ├── CMakeLists.txt
│   ├── aws_iot_client.c          # Main application
│   ├── wifi_manager.c            # WiFi connectivity
│   ├── certificate_manager.c     # AWS IoT certificates
│   └── device_shadow.c           # Device shadow handling
├── certificates/
│   ├── aws_root_ca.pem           # AWS Root CA
│   ├── device_cert.pem           # Device certificate
│   └── device_private_key.pem    # Device private key
├── scripts/
│   ├── setup_aws_iot.ps1         # AWS IoT Thing setup
│   ├── generate_device_certs.ps1 # Certificate generation
│   └── monitor.ps1               # Serial monitoring
├── build.bat                     # Windows build script
├── build.sh                      # Linux build script
├── CMakeLists.txt                # ESP-IDF project config
├── sdkconfig.defaults            # Default configuration
└── README.md                     # Project documentation
```

## Implementation Checklist
- [x] Project requirements clarified
- [ ] Scaffold project structure
- [ ] Implement AWS IoT MQTT client
- [ ] Set up certificate management
- [ ] Create build automation
- [ ] Add device shadow support
- [ ] Implement telemetry publishing
- [ ] Add command handling
- [ ] Create setup scripts
- [ ] Write documentation
- [ ] Test AWS IoT connection

## AWS IoT Integration
- Thing Name: `esp32-s3-device-{MAC}`
- MQTT Topics:
  - Telemetry: `device/{thingName}/telemetry`
  - Commands: `device/{thingName}/commands`
  - Shadow: `$aws/things/{thingName}/shadow/update`
- TLS Port: 8883
- Endpoint: Custom AWS IoT Core endpoint

## Build Requirements
- ESP-IDF v5.3+
- AWS CLI (for certificate setup)
- OpenSSL (for certificate handling)
- Git (for version control)

## Testing Strategy
- Unit tests for WiFi and MQTT connectivity
- Integration tests with AWS IoT Core
- Shadow synchronization validation
- Certificate authentication verification
- Network resilience testing