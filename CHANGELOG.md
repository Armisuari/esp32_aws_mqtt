# Changelog

All notable changes to the ESP32-S3 AWS IoT MQTT TLS project will be documented in this file.

## [1.1.0] - 2025-12-31

### Added
- Unified build system with mode selection (`--mode wifi` or `--mode sim7600e`)
- Clean build option (`--clean` flag) for build scripts
- Dual-mode architecture documentation in README
- Cross-references between WiFi and SIM7600E documentation

### Changed
- Consolidated `build.bat`, `build_wifi.bat`, and `build_sim7600e.bat` into single `build.bat` with `--mode` parameter
- Similarly updated `build.sh` for Linux/macOS with mode selection
- Enhanced build script argument parsing and error handling
- Updated all documentation to reflect new build system
- Improved `.gitignore` by removing redundant entries

### Removed
- `build_wifi.bat` - functionality integrated into `build.bat`
- `build_sim7600e.bat` - functionality integrated into `build.bat`
- `sdkconfig.old` - temporary build file removed from repository
- Redundant `sdkconfig.old` entry from `.gitignore`

### Fixed
- Build script backward compatibility maintained (default to WiFi mode)
- Documentation consistency between WiFi and SIM7600E modes

## [1.0.0] - 2025-12-11

### Added
- Initial release of ESP32-S3 AWS IoT MQTT TLS client
- Secure MQTT over TLS connection to AWS IoT Core
- X.509 certificate-based device authentication
- WiFi management with auto-reconnect functionality
- AWS IoT Device Shadow synchronization
- Telemetry data publishing
- Remote command handling via MQTT
- Cross-platform build automation (Windows/Linux)
- Complete VS Code development environment setup
- Comprehensive project documentation

### Features
- **Security**: TLS 1.2 encryption with certificate validation
- **Connectivity**: Robust WiFi and MQTT connection handling
- **AWS Integration**: Full AWS IoT Core service integration
- **Development**: Professional IDE setup with IntelliSense
- **Build System**: Automated ESP-IDF environment detection

### Technical Details
- ESP-IDF v5.3.2 compatibility
- ESP32-S3 target support
- 892KB firmware size with 15% free space
- Modular code architecture
- Production-ready error handling