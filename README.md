# ESP32-S3 AWS IoT MQTT TLS Client

A production-ready ESP-IDF project for connecting ESP32-S3 to AWS IoT Core using secure MQTT over TLS. Features device authentication, shadow synchronization, telemetry publishing, and command handling.

## ✨ Features

- 🔒 **Secure MQTT over TLS** (port 8883) with X.509 certificate authentication
- ☁️ **AWS IoT Core Integration** with Device Shadows, telemetry, and commands  
- 📡 **WiFi Management** with robust connection handling and auto-reconnect
- 🔧 **Cross-Platform Build** automation for Windows and Linux
- 🎯 **VS Code Integration** with full IntelliSense and debugging support

## 🛠️ Quick Start

### Prerequisites
- **ESP32-S3** development board
- **ESP-IDF v5.3+** ([Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **AWS CLI** for IoT setup
- **VS Code** with ESP-IDF extension (recommended)

### Build & Flash

#### Windows (PowerShell)
```powershell
# Build the project
.\build.bat

# Flash to ESP32-S3  
idf.py flash monitor
```

#### Linux/macOS
```bash
# Build the project
./build.sh

# Flash to ESP32-S3
idf.py flash monitor
```

### VS Code Development
1. Open the project in VS Code
2. Install ESP-IDF extension
3. Use **Ctrl+Shift+P** → "Tasks: Run Task" → "ESP-IDF: Build"
4. Flash using "ESP-IDF: Flash and Monitor" task

## 📁 Project Structure

```
esp32_aws_mqtt/
├── main/                     # Application source code
│   ├── aws_iot_client.c     # Main MQTT client & app logic
│   ├── wifi_manager.c       # WiFi connection management
│   ├── certificate_manager.c # X.509 certificate handling
│   └── device_shadow.c      # AWS IoT Device Shadow sync
├── certificates/            # X.509 certificates (configure for your AWS account)
│   ├── aws_root_ca.pem     # AWS Root CA certificate
│   ├── device_cert.pem     # Device certificate
│   └── device_private_key.pem # Device private key
├── scripts/                # Automation scripts
│   ├── setup_aws_iot.ps1   # AWS IoT Thing setup
│   └── monitor.ps1         # Serial monitoring
├── .vscode/                # VS Code configuration
└── build.bat / build.sh    # Cross-platform build scripts
```

## ⚙️ Configuration

### WiFi Settings
Update WiFi credentials in `main/aws_iot_client.c`:
```c
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
```

### AWS IoT Setup
1. **Create IoT Thing**: Use `scripts/setup_aws_iot.ps1` or AWS Console
2. **Download Certificates**: Place in `certificates/` directory
3. **Update Endpoint**: Set your AWS IoT endpoint in `aws_iot_client.c`

### Serial Port
Update COM port in `.vscode/settings.json`:
```json
{
  "idf.portWin": "COM3"  // Change to your port
}
```

## 🚀 MQTT Topics

| Topic | Purpose | Example |
|-------|---------|---------|
| `device/{thingName}/telemetry` | Device telemetry data | Temperature, humidity, status |
| `device/{thingName}/commands` | Remote commands | Reboot, config update |
| `$aws/things/{thingName}/shadow/update` | Device shadow sync | State synchronization |

## 🔧 Development

### Build System
- **Windows**: `build.bat` - Automated ESP-IDF environment setup and build
- **Linux**: `build.sh` - Cross-platform build script
- **VS Code**: Integrated tasks for build, flash, and monitor

### Debugging
- **Serial Monitor**: `idf.py monitor` or VS Code task
- **ESP32 Debugging**: Use VS Code ESP-IDF debug configuration
- **Logs**: ESP_LOG* macros with configurable levels

## 📊 Memory Usage

| Component | Flash (KB) | Percentage |
|-----------|-----------|------------|
| Application | ~892 | 22% |
| ESP-IDF | ~3200 | 78% |
| **Total** | **~4092** | **100%** |

Free flash space: ~15% (600KB) available for OTA updates.

## 🛡️ Security Features

- **TLS 1.2** encryption for all communications
- **X.509 certificate** authentication  
- **AWS IoT Device Certificates** for device identity
- **Secure credential storage** in ESP32 flash
- **Certificate validation** against AWS Root CA

## 📱 Example Output

```
I (1234) AWS_IOT: WiFi connected successfully
I (1456) AWS_IOT: MQTT client started successfully  
I (1567) AWS_IOT: Connected to AWS IoT Core
I (1678) TELEMETRY: Publishing telemetry data
I (1789) SHADOW: Device shadow updated successfully
```

## 🤝 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)  
5. Open Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🆘 Support

- **ESP-IDF Issues**: [ESP-IDF GitHub](https://github.com/espressif/esp-idf/issues)
- **AWS IoT Issues**: [AWS IoT Documentation](https://docs.aws.amazon.com/iot/)
- **Project Issues**: Use GitHub Issues in this repository

---

**Built with ❤️ for ESP32-S3 and AWS IoT Core**