# TeethBrushReminder Installation Guide for M5Stack CoreS3

## Prerequisites
- M5Stack CoreS3 device
- USB-C cable
- Python 3.7 or higher
- PlatformIO (will be installed by script)

## Quick Installation Steps

### 1. Install PlatformIO

**Windows (PowerShell):**
```powershell
.\install_platformio.ps1
```

**Linux/Mac:**
```bash
python -m pip install --user platformio
```

### 2. Install Dependencies

PlatformIO will automatically download all required libraries when you build the project.

### 3. Connect Your CoreS3

1. Connect your M5Stack CoreS3 to your computer via USB-C cable
2. Make sure the device is powered on
3. Note the COM port (Windows) or device path (Linux/Mac)

### 4. Build and Upload

**Build the project:**
```bash
pio run
```

**Upload to device:**
```bash
pio run -t upload
```

**Monitor serial output:**
```bash
pio device monitor
```

### 5. Set the Time

After first upload, the RTC will be set to a default time. You can set the correct time by:

**Option A: Using Serial Monitor**
- Open serial monitor at 115200 baud
- Send time commands (implementation depends on your needs)

**Option B: Modify code**
- Edit `src/main.cpp` in the `setupRTC()` function
- Set the correct date/time in the default time section
- Re-upload the firmware

## Features

- **Daily Reminders**: Shows reminders at 7:00 AM and 9:00 PM
- **Visual Display**: Large text on LCD screen with colors
- **Audio Alert**: Beep sound when reminder appears
- **Button Dismiss**: Press any button (A, B, or C) to dismiss reminder
- **Clock Display**: Shows current time and date when idle
- **Auto-dismiss**: Reminders automatically dismiss after 30 seconds

## Customization

### Change Reminder Times

Edit `src/main.cpp`:
```cpp
#define MORNING_HOUR 7
#define MORNING_MINUTE 0
#define EVENING_HOUR 21
#define EVENING_MINUTE 0
```

### Change Reminder Duration

Edit `src/main.cpp`:
```cpp
#define REMINDER_DISPLAY_DURATION 30000  // 30 seconds in milliseconds
```

## Troubleshooting

### Port Not Showing
- Install USB drivers for ESP32-S3
- Try a different USB cable
- Check Device Manager (Windows) for COM ports

### Upload Fails
- Hold the BOOT button on CoreS3 while clicking Upload
- Try pressing RESET button before uploading
- Check USB cable connection

### RTC Not Working
- The RTC requires battery backup to maintain time
- Ensure CoreS3 battery is charged
- Time will reset on power loss if battery is dead

### Display Not Showing
- Check if CoreS3 is powered on
- Try pressing RESET button
- Check serial monitor for error messages

### Buttons Not Responding
- Wait for reminder to appear first
- Buttons only work when reminder is active
- Check serial monitor for button press messages

## Project Structure

```
TeethBrushReminder/
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp            # Main application code
├── install_platformio.ps1  # PlatformIO installation script
└── INSTALL.md              # This file
```

## Additional Resources

- [M5Stack CoreS3 Documentation](https://docs.m5stack.com/en/core/cores3)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [M5Unified Library](https://github.com/m5stack/M5Unified)

## Notes

- The device must be powered continuously for reminders to work
- RTC time is maintained by battery when device is off
- Reminders trigger exactly at the set hour and minute
- Only one reminder per time slot per day

