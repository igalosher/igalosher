# TeethBrushReminder

A simple reminder application to help you remember to brush your teeth twice daily.

## Features

- Daily reminders at 7:00 AM and 9:00 PM
- Visual display on LCD screen (CoreS3) or desktop notifications (Python)
- Audio alerts (CoreS3)
- Button controls to dismiss reminders (CoreS3)
- Easy to customize reminder times

## Versions

This project supports two versions:

### 1. Python Desktop Version
- Desktop notifications (Windows)
- Runs on your computer
- See [Python Installation](#python-desktop-version) below

### 2. M5Stack CoreS3 Embedded Version
- LCD display with clock
- Audio alerts
- Button controls
- Battery powered
- See [CoreS3 Installation](#m5stack-cores3-version) below

---

## Python Desktop Version

### Installation

1. Install Python 3.7 or higher
2. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

### Usage

Run the application:
```bash
python main.py
```

The application will run in the background and send notifications at the scheduled times.

### Customization

Edit `main.py` to change reminder times:
```python
schedule.every().day.at("07:00").do(remind_brush_teeth)  # Morning
schedule.every().day.at("21:00").do(remind_brush_teeth)  # Evening
```

### Requirements

- Python 3.7+
- Windows (for notifications)
- See `requirements.txt` for Python dependencies

---

## M5Stack CoreS3 Version

### Installation

See [INSTALL.md](INSTALL.md) for detailed installation instructions.

**Quick Start:**

1. **Install PlatformIO:**
   ```powershell
   .\install_platformio.ps1
   ```

2. **Connect CoreS3:**
   - Connect your M5Stack CoreS3 via USB-C cable
   - Power on the device

3. **Upload Firmware:**
   ```powershell
   .\upload.ps1
   ```
   
   Or manually:
   ```bash
   pio run -t upload
   ```

### Usage

After uploading, the CoreS3 will:
- Display current time and date on the LCD screen
- Show reminders at 7:00 AM and 9:00 PM
- Play a beep sound when reminder appears
- Allow dismissing reminders with any button (A, B, or C)

### Customization

Edit `src/main.cpp` to change reminder times:
```cpp
#define MORNING_HOUR 7
#define MORNING_MINUTE 0
#define EVENING_HOUR 21
#define EVENING_MINUTE 0
```

### Features

- **LCD Display**: Shows time, date, and reminder messages
- **Audio Alerts**: Beep sound when reminder appears
- **Button Controls**: Press any button to dismiss reminder
- **RTC**: Real-time clock maintains time even when powered off (with battery)
- **Auto-dismiss**: Reminders automatically dismiss after 30 seconds

### Requirements

- M5Stack CoreS3 device
- USB-C cable
- Python 3.7+ (for PlatformIO)
- PlatformIO (install via script)

### Troubleshooting

See [INSTALL.md](INSTALL.md) for troubleshooting tips.

---

## Project Structure

```
TeethBrushReminder/
├── main.py                 # Python desktop version
├── requirements.txt        # Python dependencies
├── platformio.ini         # PlatformIO configuration (CoreS3)
├── src/
│   └── main.cpp           # CoreS3 firmware source code
├── install_platformio.ps1 # PlatformIO installation script
├── upload.ps1             # Upload script for CoreS3
├── INSTALL.md             # Detailed installation guide
└── README.md              # This file
```

