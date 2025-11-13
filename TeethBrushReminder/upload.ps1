# Upload script for TeethBrushReminder to CoreS3
Write-Host "=== Uploading TeethBrushReminder to CoreS3 ===" -ForegroundColor Cyan
Write-Host ""

# Check if PlatformIO is installed
Write-Host "Checking for PlatformIO..." -ForegroundColor Yellow
try {
    $pioVersion = pio --version 2>&1
    if ($pioVersion -match "PlatformIO") {
        Write-Host "✓ PlatformIO found: $pioVersion" -ForegroundColor Green
    } else {
        throw "PlatformIO not found"
    }
} catch {
    Write-Host "✗ PlatformIO not found" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install PlatformIO first:" -ForegroundColor Yellow
    Write-Host "1. Run: .\install_platformio.ps1" -ForegroundColor White
    Write-Host "2. Restart your terminal" -ForegroundColor White
    exit 1
}

Write-Host ""
Write-Host "Make sure your CoreS3 is connected via USB-C" -ForegroundColor Yellow
Write-Host "Press any key to continue or Ctrl+C to cancel..." -ForegroundColor Yellow
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")

Write-Host ""
Write-Host "Building project..." -ForegroundColor Cyan
pio run

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Uploading to CoreS3..." -ForegroundColor Cyan
    pio run -t upload
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "✓ Upload successful!" -ForegroundColor Green
        Write-Host ""
        Write-Host "To monitor serial output, run:" -ForegroundColor Cyan
        Write-Host "  pio device monitor" -ForegroundColor White
    } else {
        Write-Host ""
        Write-Host "✗ Upload failed. Please check:" -ForegroundColor Red
        Write-Host "  - CoreS3 is connected via USB-C" -ForegroundColor White
        Write-Host "  - Correct COM port is selected" -ForegroundColor White
        Write-Host "  - Try holding BOOT button during upload" -ForegroundColor White
    }
} else {
    Write-Host ""
    Write-Host "✗ Build failed. Please check the error messages above." -ForegroundColor Red
}

