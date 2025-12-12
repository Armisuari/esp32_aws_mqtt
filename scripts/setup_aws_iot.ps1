# AWS IoT Thing and Certificate Setup Script
# This script creates AWS IoT Thing, certificates, and policies

param(
    [string]$ThingName = "esp32-s3-device",
    [string]$PolicyName = "ESP32DevicePolicy",
    [string]$Region = "us-east-1"
)

$ErrorActionPreference = "Stop"

Write-Host "================================================" -ForegroundColor Green
Write-Host "AWS IoT Thing Setup for ESP32-S3" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green

# Check if AWS CLI is installed
$awsCmd = $null
$possiblePaths = @(
    "aws",  # In PATH
    "C:\Program Files\Amazon\AWSCLIV2\aws.exe",
    "$env:PROGRAMFILES\Amazon\AWSCLIV2\aws.exe",
    "$env:LOCALAPPDATA\Programs\Python\Python*\Scripts\aws.exe"
)

foreach ($path in $possiblePaths) {
    try {
        if ($path -eq "aws") {
            $null = Get-Command aws -ErrorAction Stop
            $awsCmd = "aws"
            break
        } elseif (Test-Path $path) {
            $awsCmd = $path
            break
        }
    } catch {
        continue
    }
}

if (-not $awsCmd) {
    Write-Host "ERROR: AWS CLI not found. Please install AWS CLI first." -ForegroundColor Red
    Write-Host "Download from: https://aws.amazon.com/cli/" -ForegroundColor Yellow
    Write-Host "After installation, restart PowerShell or add AWS CLI to your PATH." -ForegroundColor Yellow
    exit 1
}

try {
    $awsVersion = & $awsCmd --version 2>&1
    Write-Host "AWS CLI found: $awsVersion" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to execute AWS CLI." -ForegroundColor Red
    exit 1
}

# Check AWS credentials
try {
    $identity = & $awsCmd sts get-caller-identity --output json | ConvertFrom-Json
    Write-Host "AWS Identity: $($identity.Arn)" -ForegroundColor Green
} catch {
    Write-Host "ERROR: AWS credentials not configured." -ForegroundColor Red
    Write-Host "Run: $awsCmd configure" -ForegroundColor Yellow
    exit 1
}

# Create certificates directory
$certDir = "certificates"
if (!(Test-Path $certDir)) {
    New-Item -ItemType Directory -Path $certDir
    Write-Host "Created certificates directory" -ForegroundColor Green
}

Write-Host "Creating AWS IoT Thing: $ThingName" -ForegroundColor Yellow

# Create IoT Thing
try {
    $thing = & $awsCmd iot create-thing --thing-name $ThingName --region $Region --output json | ConvertFrom-Json
    Write-Host "Thing created: $($thing.thingArn)" -ForegroundColor Green
} catch {
    Write-Host "Thing might already exist, continuing..." -ForegroundColor Yellow
}

# Create certificates
Write-Host "Creating device certificates..." -ForegroundColor Yellow
try {
    $cert = & $awsCmd iot create-keys-and-certificate --set-as-active --region $Region --output json | ConvertFrom-Json
    
    # Save certificate
    $cert.certificatePem | Out-File -FilePath "$certDir\device_cert.pem" -Encoding ascii
    Write-Host "Device certificate saved to $certDir\device_cert.pem" -ForegroundColor Green
    
    # Save private key
    $cert.keyPair.PrivateKey | Out-File -FilePath "$certDir\device_private_key.pem" -Encoding ascii
    Write-Host "Private key saved to $certDir\device_private_key.pem" -ForegroundColor Green
    
    $certificateArn = $cert.certificateArn
    Write-Host "Certificate ARN: $certificateArn" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to create certificates" -ForegroundColor Red
    exit 1
}

# Download AWS Root CA certificate
Write-Host "Downloading AWS Root CA certificate..." -ForegroundColor Yellow
try {
    $rootCaUrl = "https://www.amazontrust.com/repository/AmazonRootCA1.pem"
    Invoke-WebRequest -Uri $rootCaUrl -OutFile "$certDir\aws_root_ca.pem"
    Write-Host "AWS Root CA certificate saved to $certDir\aws_root_ca.pem" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to download AWS Root CA certificate" -ForegroundColor Red
    exit 1
}

# Create IoT Policy
Write-Host "Creating IoT Policy: $PolicyName" -ForegroundColor Yellow

# Build policy document with proper JSON formatting
$policyJson = @{
    Version = "2012-10-17"
    Statement = @(
        @{
            Effect = "Allow"
            Action = @(
                "iot:Connect",
                "iot:Publish", 
                "iot:Subscribe",
                "iot:Receive"
            )
            Resource = @(
                "arn:aws:iot:${Region}:*:client/${ThingName}",
                "arn:aws:iot:${Region}:*:topic/device/${ThingName}/*",
                "arn:aws:iot:${Region}:*:topicfilter/device/${ThingName}/*",
                "arn:aws:iot:${Region}:*:topic/`$aws/things/${ThingName}/shadow/*",
                "arn:aws:iot:${Region}:*:topicfilter/`$aws/things/${ThingName}/shadow/*"
            )
        }
    )
} | ConvertTo-Json -Depth 3

try {
    $tempPolicy = New-TemporaryFile
    $policyJson | Out-File -FilePath $tempPolicy.FullName -Encoding utf8
    & $awsCmd iot create-policy --policy-name $PolicyName --policy-document file://$($tempPolicy.FullName) --region $Region
    Remove-Item $tempPolicy.FullName
    Write-Host "Policy created: $PolicyName" -ForegroundColor Green
} catch {
    Write-Host "Policy might already exist, continuing..." -ForegroundColor Yellow
}

# Attach policy to certificate
Write-Host "Attaching policy to certificate..." -ForegroundColor Yellow
try {
    & $awsCmd iot attach-principal-policy --policy-name $PolicyName --principal $cert.certificateArn --region $Region
    Write-Host "Policy attached to certificate" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to attach policy" -ForegroundColor Red
    exit 1
}

# Attach certificate to thing
Write-Host "Attaching certificate to thing..." -ForegroundColor Yellow
try {
    & $awsCmd iot attach-thing-principal --thing-name $ThingName --principal $cert.certificateArn --region $Region
    Write-Host "Certificate attached to thing" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to attach certificate to thing" -ForegroundColor Red
    exit 1
}

# Get IoT endpoint
Write-Host "Getting IoT endpoint..." -ForegroundColor Yellow
try {
    $endpoint = & $awsCmd iot describe-endpoint --endpoint-type iot:Data-ATS --region $Region --output text
    Write-Host "IoT Endpoint: $endpoint" -ForegroundColor Green
} catch {
    Write-Host "ERROR: Failed to get IoT endpoint" -ForegroundColor Red
    exit 1
}

Write-Host "================================================" -ForegroundColor Green
Write-Host "AWS IoT Setup Complete!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Configuration Summary:" -ForegroundColor Yellow
Write-Host "  Thing Name: $ThingName" -ForegroundColor White
Write-Host "  Policy Name: $PolicyName" -ForegroundColor White
Write-Host "  IoT Endpoint: $endpoint" -ForegroundColor White
Write-Host "  AWS Root CA: $certDir\aws_root_ca.pem" -ForegroundColor White
Write-Host "  Device Certificate: $certDir\device_cert.pem" -ForegroundColor White
Write-Host "  Private Key: $certDir\device_private_key.pem" -ForegroundColor White
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "1. Update sdkconfig with WiFi credentials" -ForegroundColor White
Write-Host "2. Update sdkconfig with AWS IoT endpoint and thing name" -ForegroundColor White
Write-Host "3. Build and flash the firmware" -ForegroundColor White
Write-Host ""
Write-Host "Example sdkconfig values:" -ForegroundColor Yellow
Write-Host "CONFIG_ESP_WIFI_SSID=`"YourWiFiSSID`"" -ForegroundColor Gray
Write-Host "CONFIG_ESP_WIFI_PASSWORD=`"YourWiFiPassword`"" -ForegroundColor Gray
Write-Host "CONFIG_AWS_IOT_MQTT_HOST=`"$endpoint`"" -ForegroundColor Gray
Write-Host "CONFIG_AWS_IOT_DEVICE_THING_NAME=`"$ThingName`"" -ForegroundColor Gray
Write-Host "================================================" -ForegroundColor Green