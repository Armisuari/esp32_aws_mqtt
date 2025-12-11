# ESP32 Serial Monitor Script
# Automated serial monitoring with port detection

param(
    [string]$Port = "",
    [int]$Baud = 115200
)

Write-Host "================================================" -ForegroundColor Green
Write-Host "ESP32-S3 Serial Monitor" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green

# Check if ESP-IDF is available
if (-not $env:IDF_PATH) {
    Write-Host "ERROR: ESP-IDF environment not configured" -ForegroundColor Red
    Write-Host "Run the build script first to setup environment" -ForegroundColor Yellow
    exit 1
}

# Auto-detect COM port if not specified
if ($Port -eq "") {
    Write-Host "Auto-detecting ESP32 port..." -ForegroundColor Yellow
    
    # Get all COM ports
    $comPorts = [System.IO.Ports.SerialPort]::getportnames() | Sort-Object
    
    if ($comPorts.Count -eq 0) {
        Write-Host "ERROR: No COM ports found" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Available COM ports:" -ForegroundColor Yellow
    foreach ($comPort in $comPorts) {
        Write-Host "  $comPort" -ForegroundColor White
    }
    
    # Use the highest numbered COM port (usually the most recent)
    $Port = $comPorts[-1]
    Write-Host "Using port: $Port" -ForegroundColor Green
}

Write-Host "Starting serial monitor..." -ForegroundColor Yellow
Write-Host "Port: $Port" -ForegroundColor White
Write-Host "Baud: $Baud" -ForegroundColor White
Write-Host ""
Write-Host "Press Ctrl+C to exit monitor" -ForegroundColor Yellow
Write-Host "================================================" -ForegroundColor Green

# Start IDF monitor
try {
    idf.py -p $Port monitor
} catch {
    Write-Host "ERROR: Failed to start monitor" -ForegroundColor Red
    Write-Host "Make sure ESP32 is connected and drivers are installed" -ForegroundColor Yellow
    exit 1
}