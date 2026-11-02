# Mini-E-Reader Clean Build & Package Helper (Windows PowerShell)
$ErrorActionPreference = "Stop"

Write-Host "=== Building Mini-E-Reader Firmware (ESP32-S3) ===" -ForegroundColor Cyan

# 1. Lock Target to ESP32-S3 in Environment
$env:IDF_TARGET = "esp32s3"

# 2. Check if build directory needs initial configuration
if (!(Test-Path "build")) {
    Write-Host "[1/3] Setting target to esp32s3..." -ForegroundColor Yellow
    idf.py set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] Failed to set target." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# 3. Build Firmware
Write-Host "[2/3] Compiling firmware..." -ForegroundColor Yellow
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Firmware compilation failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

# 4. Package Unified Binary and Sync to PWA
Write-Host "[3/3] Packaging unified factory binary (0x0)..." -ForegroundColor Yellow
if (Test-Path "package_firmware.py") {
    python package_firmware.py
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n[ERROR] Packaging unified binary failed." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host "`n=== Build & Packaging Complete! ===" -ForegroundColor Green
Write-Host "To flash your device, run:" -ForegroundColor White
Write-Host "  idf.py -p COM3 flash monitor" -ForegroundColor Cyan
Write-Host "Or flash via browser at pwa/index.html (Firmware Flasher tab)`n" -ForegroundColor White
