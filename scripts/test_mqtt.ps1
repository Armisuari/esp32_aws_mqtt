# Test AWS IoT MQTT connectivity using AWS CLI
# Run this script to verify MQTT service is working

param(
    [string]$ThingName = "esp32-s3-device",
    [string]$Region = "ap-southeast-1"
)

$awsCmd = "C:\Program Files\Amazon\AWSCLIV2\aws.exe"

Write-Host "================================================" -ForegroundColor Green
Write-Host "Testing AWS IoT MQTT Service" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green

# Get IoT endpoint
Write-Host "Getting IoT endpoint..." -ForegroundColor Yellow
try {
    $endpoint = & $awsCmd iot describe-endpoint --endpoint-type iot:Data-ATS --region $Region --output text
    Write-Host "IoT Endpoint: $endpoint" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to get IoT endpoint" -ForegroundColor Red
    exit 1
}

# Test publish to telemetry topic
Write-Host "Testing publish to telemetry topic..." -ForegroundColor Yellow
$testMessage = @{
    timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    device_id = $ThingName
    test_message = "Hello from AWS CLI"
    free_heap = 256000
    uptime_ms = 60000
} | ConvertTo-Json

try {
    $escapedMessage = $testMessage -replace '"', '\"'
    & $awsCmd iot-data publish --topic "device/$ThingName/telemetry" --payload "$escapedMessage" --region $Region
    Write-Host "✅ Successfully published to telemetry topic!" -ForegroundColor Green
} catch {
    Write-Host "❌ Failed to publish to telemetry topic: $($_.Exception.Message)" -ForegroundColor Red
}

# Test shadow update
Write-Host "Testing shadow update..." -ForegroundColor Yellow
$shadowUpdate = @{
    state = @{
        reported = @{
            status = "online"
            test_mode = $true
            timestamp = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
        }
    }
} | ConvertTo-Json -Depth 3

try {
    # Write JSON to temp file for proper handling
    $tempFile = New-TemporaryFile
    $shadowUpdate | Out-File -FilePath $tempFile.FullName -Encoding utf8
    & $awsCmd iot-data update-thing-shadow --thing-name $ThingName --payload file://$($tempFile.FullName) --region $Region thing-shadow-output.json
    Remove-Item $tempFile.FullName
    Write-Host "✅ Successfully updated device shadow!" -ForegroundColor Green
    
    # Read shadow response
    if (Test-Path "thing-shadow-output.json") {
        $shadowResponse = Get-Content "thing-shadow-output.json" | ConvertFrom-Json
        Write-Host "Shadow version: $($shadowResponse.metadata.reported.timestamp)" -ForegroundColor Cyan
        Remove-Item "thing-shadow-output.json"
    }
} catch {
    Write-Host "❌ Failed to update shadow: $($_.Exception.Message)" -ForegroundColor Red
}

# Test get shadow
Write-Host "Testing get shadow..." -ForegroundColor Yellow
try {
    & $awsCmd iot-data get-thing-shadow --thing-name $ThingName --region $Region shadow-output.json
    Write-Host "✅ Successfully retrieved device shadow!" -ForegroundColor Green
    
    if (Test-Path "shadow-output.json") {
        $shadow = Get-Content "shadow-output.json" | ConvertFrom-Json
        Write-Host "Current shadow state:" -ForegroundColor Cyan
        Write-Host ($shadow.state | ConvertTo-Json) -ForegroundColor White
        Remove-Item "shadow-output.json"
    }
} catch {
    Write-Host "❌ Failed to get shadow: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host "================================================" -ForegroundColor Green
Write-Host "MQTT Test Complete!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "1. If tests passed ✅, your AWS IoT MQTT service is working!" -ForegroundColor White
Write-Host "2. Update your ESP32 configuration with endpoint: $endpoint" -ForegroundColor White
Write-Host "3. Build and flash your ESP32 firmware" -ForegroundColor White
Write-Host "4. Monitor ESP32 logs for MQTT connection" -ForegroundColor White