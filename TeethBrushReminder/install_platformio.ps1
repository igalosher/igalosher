# Install PlatformIO for M5Stack CoreS3 Upload
Write-Host "=== Installing PlatformIO ===" -ForegroundColor Cyan
Write-Host ""

# Check if Python is available
Write-Host "Checking for Python..." -ForegroundColor Yellow
try {
    $pythonVersion = python --version 2>&1
    if ($pythonVersion -match "Python") {
        Write-Host "✓ $pythonVersion found" -ForegroundColor Green
    } else {
        throw "Python not found"
    }
} catch {
    Write-Host "✗ Python not found" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Python first:" -ForegroundColor Yellow
    Write-Host "1. Download from: https://www.python.org/downloads/" -ForegroundColor White
    Write-Host "2. During installation, check 'Add Python to PATH'" -ForegroundColor White
    Write-Host "3. Restart this script after installation" -ForegroundColor White
    exit 1
}

Write-Host ""
Write-Host "Installing PlatformIO..." -ForegroundColor Yellow
python -m pip install --user platformio

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "✓ PlatformIO installed successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "To upload to your CoreS3 device:" -ForegroundColor Cyan
    Write-Host "1. Connect your M5Stack CoreS3 via USB-C" -ForegroundColor White
    Write-Host "2. Run: pio run -t upload" -ForegroundColor White
    Write-Host ""
    Write-Host "Note: You may need to restart your terminal for 'pio' command to be available" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "✗ Installation failed. Please check the error messages above." -ForegroundColor Red
}

