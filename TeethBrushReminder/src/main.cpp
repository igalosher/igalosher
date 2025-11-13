#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include "esp_camera.h"

// Reminder configuration - only one reminder per day cycle (4:00 AM to 3:59 AM)
// Reminder is triggered by face recognition, not by time

// WiFi and NTP configuration for automatic time sync
const char* ssid = "Mad_Dog_Net";
const char* password = "00400040";
const char* ntpServer = "pool.ntp.org";
// Timezone offset: GMT offset in seconds (e.g., -5*3600 for EST, -8*3600 for PST)
// For EST: -5*3600, for PST: -8*3600, for UTC: 0
const long gmtOffset_sec = 0;  // Adjust for your timezone
const int daylightOffset_sec = 3600;  // 1 hour for daylight saving (if applicable)

// Reminder duration (milliseconds)
#define REMINDER_DISPLAY_DURATION 30000  // 30 seconds

// Button debounce
#define BUTTON_DEBOUNCE_MS 200

// State management
enum ReminderState {
    IDLE,
    REMINDER_ACTIVE,
    DISMISSED
};

ReminderState currentState = IDLE;
unsigned long reminderStartTime = 0;
bool reminderShown = false;
bool reminderDismissed = false;  // Track if reminder was dismissed in current cycle
unsigned long lastButtonPress = 0;
unsigned long lastFaceCheck = 0;
#define FACE_CHECK_INTERVAL 2000  // Check for face every 2 seconds (camera needs time)
bool cameraInitialized = false;
uint8_t* previousFrame = NULL;
size_t previousFrameSize = 0;
unsigned long lastCameraPreview = 0;
#define CAMERA_PREVIEW_INTERVAL 100  // Update camera preview every 100ms (10 FPS)
#define CAMERA_PREVIEW_WIDTH 80   // Preview size in bottom right
#define CAMERA_PREVIEW_HEIGHT 60
bool lastFaceDetectionResult = false;
unsigned long lastFaceDetectionTime = 0;
#define FACE_DETECTION_DISPLAY_TIMEOUT 3000  // Show "Face Detected" for 3 seconds after detection

// Camera pin definitions for CoreS3 (GC0308 camera)
#define CAMERA_PIN_PWDN    -1
#define CAMERA_PIN_RESET   -1
#define CAMERA_PIN_XCLK    21
#define CAMERA_PIN_SIOD    12
#define CAMERA_PIN_SIOC    9
#define CAMERA_PIN_D7      47
#define CAMERA_PIN_D6      48
#define CAMERA_PIN_D5      16
#define CAMERA_PIN_D4      15
#define CAMERA_PIN_D3      14
#define CAMERA_PIN_D2      13
#define CAMERA_PIN_D1      11
#define CAMERA_PIN_D0      10
#define CAMERA_PIN_VSYNC   8
#define CAMERA_PIN_HREF    4
#define CAMERA_PIN_PCLK    5

// RTC time structure - initialize with valid defaults
m5::rtc_datetime_t rtcDateTime = {
    .date = {.year = 2024, .month = 1, .date = 1, .weekDay = 1},
    .time = {.hours = 12, .minutes = 0, .seconds = 0}
};
time_t lastSync = 0;
bool timeInitialized = false;

// WiFi status
bool wifiConnected = false;
bool wifiLastKnownState = false;
String wifiSSID = "";
unsigned long lastWifiCheck = 0;
#define WIFI_CHECK_INTERVAL 10000  // Check WiFi status every 10 seconds
#define WIFI_QUICK_CHECK_TIMEOUT 3000  // Quick WiFi check timeout (3 seconds)

void syncTimeFromNTP() {
    Serial.println("=== Starting NTP Time Sync ===");
    Serial.printf("SSID: %s\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.println("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connected!");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            Serial.print("RSSI: ");
            Serial.println(WiFi.RSSI());
            wifiConnected = true;
            wifiLastKnownState = true;
            wifiSSID = WiFi.SSID();
        
        // Configure NTP
        Serial.println("Configuring NTP...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // Wait a bit for NTP to initialize
        delay(2000);
        
        // Wait for time to be set (with timeout)
        struct tm timeinfo;
        int retry = 0;
        Serial.println("Waiting for NTP time...");
        while (!getLocalTime(&timeinfo, 5000) && retry < 30) {  // Wait up to 5 seconds per attempt, 30 attempts = 150 seconds max
            delay(500);
            Serial.print(".");
            retry++;
        }
        Serial.println();
        
        if (getLocalTime(&timeinfo)) {
            Serial.println("NTP time received!");
            Serial.printf("NTP Date: %04d-%02d-%02d\n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
            Serial.printf("NTP Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            
            // Convert to RTC format and set
            rtcDateTime.date.year = timeinfo.tm_year + 1900;
            rtcDateTime.date.month = timeinfo.tm_mon + 1;
            rtcDateTime.date.date = timeinfo.tm_mday;
            rtcDateTime.time.hours = timeinfo.tm_hour;
            rtcDateTime.time.minutes = timeinfo.tm_min;
            rtcDateTime.time.seconds = timeinfo.tm_sec;
            
            // Set RTC
            M5.Rtc.setDateTime(&rtcDateTime);
            timeInitialized = true;
            Serial.println("RTC set successfully!");
            Serial.printf("RTC Date: %04d-%02d-%02d\n", rtcDateTime.date.year, rtcDateTime.date.month, rtcDateTime.date.date);
            Serial.printf("RTC Time: %02d:%02d:%02d\n", rtcDateTime.time.hours, rtcDateTime.time.minutes, rtcDateTime.time.seconds);
            
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return;
        } else {
            Serial.println("ERROR: Failed to get NTP time!");
        }
    } else {
        Serial.println("ERROR: WiFi connection failed!");
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
    }
    
    Serial.println("Failed to sync time from NTP");
    wifiConnected = false;
    wifiLastKnownState = false;
    wifiSSID = "";
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// Parse compile-time date string (e.g., "Jan 01 2024") to date components
void parseCompileDate(const char* dateStr, int& year, int& month, int& day) {
    // Format: "MMM DD YYYY" or "MMM  D YYYY"
    char monthStr[4] = {0};
    sscanf(dateStr, "%3s %d %d", monthStr, &day, &year);
    
    // Convert month name to number
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    month = 1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(monthStr, months[i], 3) == 0) {
            month = i + 1;
            break;
        }
    }
}

// Parse compile-time time string (e.g., "12:34:56") to time components
void parseCompileTime(const char* timeStr, int& hour, int& minute, int& second) {
    sscanf(timeStr, "%d:%d:%d", &hour, &minute, &second);
}

void setupRTC() {
    Serial.println("=== Initializing RTC ===");
    
    // Initialize RTC
    if (!M5.Rtc.begin()) {
        Serial.println("ERROR: RTC initialization failed!");
        // Set a default valid time structure
        rtcDateTime.date.year = 2024;
        rtcDateTime.date.month = 1;
        rtcDateTime.date.date = 1;
        rtcDateTime.time.hours = 12;
        rtcDateTime.time.minutes = 0;
        rtcDateTime.time.seconds = 0;
        timeInitialized = false;
        return;
    }
    Serial.println("RTC initialized");
    
    // Try to get time from RTC - check return value!
    bool rtcValid = M5.Rtc.getDateTime(&rtcDateTime);
    
    if (!rtcValid) {
        Serial.println("WARNING: RTC getDateTime() returned false - RTC may not be initialized");
        // Set default valid time
        rtcDateTime.date.year = 2024;
        rtcDateTime.date.month = 1;
        rtcDateTime.date.date = 1;
        rtcDateTime.date.weekDay = 1;
        rtcDateTime.time.hours = 12;
        rtcDateTime.time.minutes = 0;
        rtcDateTime.time.seconds = 0;
        M5.Rtc.setDateTime(&rtcDateTime);
        timeInitialized = false;
    } else {
        // Validate the time structure
        if (rtcDateTime.date.year < 2000 || rtcDateTime.date.year > 2100 ||
            rtcDateTime.date.month < 1 || rtcDateTime.date.month > 12 ||
            rtcDateTime.date.date < 1 || rtcDateTime.date.date > 31 ||
            rtcDateTime.time.hours > 23 || rtcDateTime.time.minutes > 59 || rtcDateTime.time.seconds > 59) {
            Serial.println("WARNING: RTC time is invalid, resetting...");
            rtcDateTime.date.year = 2024;
            rtcDateTime.date.month = 1;
            rtcDateTime.date.date = 1;
            rtcDateTime.date.weekDay = 1;
            rtcDateTime.time.hours = 12;
            rtcDateTime.time.minutes = 0;
            rtcDateTime.time.seconds = 0;
            M5.Rtc.setDateTime(&rtcDateTime);
            timeInitialized = false;
        }
    }
    
    Serial.printf("Current RTC time: %04d-%02d-%02d %02d:%02d:%02d\n", 
                   rtcDateTime.date.year, rtcDateTime.date.month, rtcDateTime.date.date,
                   rtcDateTime.time.hours, rtcDateTime.time.minutes, rtcDateTime.time.seconds);
    
    // Always try to sync from NTP first (to get accurate time)
    // Force sync if time looks like default (2024-01-01 12:00:00) or if year is 2024
    bool shouldSync = true;
    if (rtcDateTime.date.year >= 2020 && rtcDateTime.date.year <= 2100) {
        // Check if time seems like default time
        if (rtcDateTime.date.year == 2024 && rtcDateTime.date.month == 1 && 
            rtcDateTime.date.date == 1 && rtcDateTime.time.hours == 12 && 
            rtcDateTime.time.minutes == 0) {
            // This looks like default time, force sync
            Serial.println("RTC has default time (2024-01-01 12:00), forcing sync...");
            shouldSync = true;
        } else if (rtcDateTime.date.year == 2024) {
            // Year is 2024 but not default - still sync to get current date
            Serial.println("RTC year is 2024, syncing to get current date/time...");
            shouldSync = true;
        } else {
            // Time seems valid, but still try NTP to keep it accurate
            Serial.println("RTC has valid time, syncing from NTP for accuracy...");
            shouldSync = true;
        }
    }
    
    if (shouldSync) {
        syncTimeFromNTP();
    }
    
    // If NTP sync failed, use compile-time (PC time during compilation)
    if (!timeInitialized) {
        Serial.println("WARNING: NTP sync failed, using PC compile time");
        
        // Get compile-time date and time (from PC when code was compiled)
        const char* compileDate = __DATE__;  // "Jan 01 2024"
        const char* compileTime = __TIME__;  // "12:34:56"
        
        int year, month, day, hour, minute, second;
        parseCompileDate(compileDate, year, month, day);
        parseCompileTime(compileTime, hour, minute, second);
        
        rtcDateTime.date.year = year;
        rtcDateTime.date.month = month;
        rtcDateTime.date.date = day;
        rtcDateTime.time.hours = hour;
        rtcDateTime.time.minutes = minute;
        rtcDateTime.time.seconds = second;
        
        M5.Rtc.setDateTime(&rtcDateTime);
        timeInitialized = true;
        
        Serial.printf("Set time from PC compile time: %04d-%02d-%02d %02d:%02d:%02d\n",
                     year, month, day, hour, minute, second);
    }
    
    // Verify final time - check return value!
    if (M5.Rtc.getDateTime(&rtcDateTime)) {
        Serial.printf("Final RTC time: %04d-%02d-%02d %02d:%02d:%02d\n", 
                       rtcDateTime.date.year, rtcDateTime.date.month, rtcDateTime.date.date,
                       rtcDateTime.time.hours, rtcDateTime.time.minutes, rtcDateTime.time.seconds);
    } else {
        Serial.println("WARNING: Could not read final RTC time");
    }
    Serial.println("=== RTC Setup Complete ===");
}

void updateTime() {
    if (M5.Rtc.isEnabled()) {
        // Check return value - getDateTime returns bool!
        m5::rtc_datetime_t newTime;
        if (M5.Rtc.getDateTime(&newTime)) {
            // Validate time after reading
            if (newTime.date.year >= 2000 && newTime.date.year <= 2100 &&
                newTime.date.month >= 1 && newTime.date.month <= 12 &&
                newTime.date.date >= 1 && newTime.date.date <= 31 &&
                newTime.time.hours <= 23 && newTime.time.minutes <= 59 && newTime.time.seconds <= 59) {
                // Time is valid, update it
                rtcDateTime = newTime;
                lastSync = millis();
            }
            // If invalid, keep previous valid time
        }
        // If getDateTime failed, keep previous valid time
    }
}

void updateWiFiStatus() {
    unsigned long currentTime = millis();
    if (currentTime - lastWifiCheck > WIFI_CHECK_INTERVAL) {
        lastWifiCheck = currentTime;
        
        // Check current WiFi status
        wifiConnected = (WiFi.status() == WL_CONNECTED);
        
        if (wifiConnected) {
            wifiSSID = WiFi.SSID();
            wifiLastKnownState = true;
        } else {
            // If WiFi is off, try a quick connection check
            if (WiFi.getMode() == WIFI_OFF) {
                // WiFi is off, try to check if we can connect
                WiFi.mode(WIFI_STA);
                WiFi.begin(ssid, password);
                
                unsigned long startTime = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_QUICK_CHECK_TIMEOUT) {
                    delay(100);
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    wifiConnected = true;
                    wifiSSID = WiFi.SSID();
                    wifiLastKnownState = true;
                    // Disconnect again to save power
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                } else {
                    wifiConnected = false;
                    wifiSSID = "";
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                }
            } else {
                // WiFi is in some other state, just check status
                wifiConnected = false;
                wifiSSID = "";
            }
        }
    }
}

void displayWiFiStatus() {
    // Display WiFi status at upper right (240x320 portrait, rotated 90° right)
    // Upper right corner: more room for SSID
    M5.Display.setTextSize(1);
    
    // Clear the area first to avoid text overlap - wider area for SSID
    M5.Display.fillRect(80, 0, 160, 20, BLACK);
    
    if (wifiConnected || wifiLastKnownState) {
        M5.Display.setTextColor(GREEN);
        M5.Display.setCursor(80, 5);
        if (wifiSSID.length() > 0) {
            // Truncate SSID if too long (now can show up to 20 chars)
            String displaySSID = wifiSSID;
            if (displaySSID.length() > 20) {
                displaySSID = displaySSID.substring(0, 20) + "...";
            }
            M5.Display.print("WiFi: ");
            M5.Display.print(displaySSID);
        } else {
            M5.Display.print("WiFi: OK");
        }
        // Draw small indicator dot
        M5.Display.fillCircle(235, 8, 3, GREEN);
    } else {
        M5.Display.setTextColor(RED);
        M5.Display.setCursor(80, 5);
        M5.Display.print("WiFi: Off");
        // Draw small indicator dot
        M5.Display.fillCircle(235, 8, 3, RED);
    }
}

void displayCameraStatus() {
    // Display camera status below WiFi status (240x320 portrait, rotated 90° right)
    M5.Display.setTextSize(1);
    
    // Clear the area first to avoid text overlap
    M5.Display.fillRect(80, 22, 160, 18, BLACK);
    
    if (cameraInitialized) {
        // Camera is on
        M5.Display.setTextColor(GREEN);
        M5.Display.setCursor(80, 25);
        M5.Display.print("Cam: ON");
        
        // Check if face was recently detected (within last 3 seconds)
        unsigned long currentTime = millis();
        bool recentFaceDetection = lastFaceDetectionResult && 
                                   (currentTime - lastFaceDetectionTime < FACE_DETECTION_DISPLAY_TIMEOUT);
        
        if (recentFaceDetection) {
            // Face detected recently
            M5.Display.setTextColor(YELLOW);
            M5.Display.setCursor(140, 25);
            M5.Display.print("Face: YES");
            // Draw indicator dot
            M5.Display.fillCircle(235, 28, 3, YELLOW);
        } else {
            // No face detected
            M5.Display.setTextColor(CYAN);
            M5.Display.setCursor(140, 25);
            M5.Display.print("Face: NO");
            // Draw indicator dot
            M5.Display.fillCircle(235, 28, 3, CYAN);
        }
    } else {
        // Camera is off/not initialized
        M5.Display.setTextColor(RED);
        M5.Display.setCursor(80, 25);
        M5.Display.print("Cam: OFF");
        M5.Display.setCursor(140, 25);
        M5.Display.print("Face: --");
        // Draw indicator dot
        M5.Display.fillCircle(235, 28, 3, RED);
    }
}

// Initialize camera for face detection
bool initCamera() {
    if (cameraInitialized) {
        return true;
    }
    
    // For CoreS3, we need to check if camera is actually present
    // The camera might not be available on all CoreS3 models (CoreS3SE doesn't have it)
    Serial.println("Attempting to initialize camera...");
    
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240 - smaller for faster processing
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
    
    // Try to initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        Serial.println("Camera may not be available on this device (CoreS3SE?)");
        cameraInitialized = false;
        return false;
    }
    
    // Test if camera actually works by trying to capture a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera init succeeded but cannot capture frames");
        esp_camera_deinit();
        cameraInitialized = false;
        return false;
    }
    esp_camera_fb_return(fb);
    
    // Configure face detection
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_framesize(s, FRAMESIZE_QVGA);
        s->set_brightness(s, 0);  // -2 to 2
        s->set_contrast(s, 0);    // -2 to 2
        s->set_saturation(s, 0);  // -2 to 2
    }
    
    cameraInitialized = true;
    Serial.println("Camera initialized successfully!");
    return true;
}

// Display camera preview in bottom right corner
void displayCameraPreview() {
    if (!cameraInitialized) {
        return;
    }
    
    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        return;
    }
    
    // Calculate preview position (bottom right in portrait mode, rotated 90° right)
    // Screen is 240x320 in portrait mode
    int previewX = 240 - CAMERA_PREVIEW_WIDTH - 5;  // 5px margin from right
    int previewY = 320 - CAMERA_PREVIEW_HEIGHT - 5;  // 5px margin from bottom
    
    // Draw border
    M5.Display.drawRect(previewX - 1, previewY - 1, CAMERA_PREVIEW_WIDTH + 2, CAMERA_PREVIEW_HEIGHT + 2, WHITE);
    
    // Scale and display camera image
    if (fb->format == PIXFORMAT_RGB565) {
        // Calculate scaling factors
        float scaleX = (float)fb->width / CAMERA_PREVIEW_WIDTH;
        float scaleY = (float)fb->height / CAMERA_PREVIEW_HEIGHT;
        
        // Sample pixels from camera frame and display
        for (int py = 0; py < CAMERA_PREVIEW_HEIGHT; py++) {
            for (int px = 0; px < CAMERA_PREVIEW_WIDTH; px++) {
                int srcX = (int)(px * scaleX);
                int srcY = (int)(py * scaleY);
                
                if (srcX < (int)fb->width && srcY < (int)fb->height) {
                    int srcIndex = srcY * fb->width + srcX;
                    uint16_t pixel = ((uint16_t*)fb->buf)[srcIndex];
                    M5.Display.drawPixel(previewX + px, previewY + py, pixel);
                }
            }
        }
    }
    
    esp_camera_fb_return(fb);
}

// Face/person detection using motion detection and brightness analysis
bool checkForFace() {
    // Initialize camera if not already done
    if (!cameraInitialized) {
        if (!initCamera()) {
            return false;  // Camera not available
        }
    }
    
    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }
    
    bool personDetected = false;
    
    // Use motion detection and brightness analysis to detect presence
    if (fb->format == PIXFORMAT_RGB565 && fb->len > 0) {
        // Allocate memory for previous frame comparison
        if (previousFrame == NULL) {
            previousFrameSize = fb->len;
            previousFrame = (uint8_t*)malloc(previousFrameSize);
            if (previousFrame == NULL) {
                Serial.println("Failed to allocate previous frame buffer");
                esp_camera_fb_return(fb);
                return false;
            }
            // Copy current frame as baseline
            memcpy(previousFrame, fb->buf, previousFrameSize);
            esp_camera_fb_return(fb);
            return false;  // First frame, no comparison possible
        }
        
        // Calculate motion by comparing frames
        uint32_t motionScore = 0;
        uint32_t brightnessScore = 0;
        uint32_t centerBrightness = 0;
        
        // Sample pixels for faster processing (every 4th pixel)
        for (size_t i = 0; i < fb->len; i += 8) {
            uint8_t current = fb->buf[i];
            uint8_t previous = previousFrame[i];
            
            // Motion detection: difference between frames
            int diff = abs((int)current - (int)previous);
            if (diff > 15) {  // Significant change
                motionScore++;
            }
            
            // Brightness analysis for center region (where face would be)
            if (i > fb->len * 0.3 && i < fb->len * 0.7) {
                uint16_t pixel = ((uint16_t*)fb->buf)[i / 2];
                uint8_t r = (pixel >> 11) & 0x1F;
                uint8_t g = (pixel >> 5) & 0x3F;
                uint8_t b = pixel & 0x1F;
                uint8_t brightness = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                centerBrightness += brightness;
                
                // Face-like brightness (skin tone range)
                if (brightness > 100 && brightness < 220) {
                    brightnessScore++;
                }
            }
        }
        
        // Update previous frame
        memcpy(previousFrame, fb->buf, previousFrameSize);
        
        // Detection logic:
        // 1. Motion detected (person moving)
        // 2. Center region has face-like brightness
        bool hasMotion = motionScore > (fb->len / 8) * 0.1;  // 10% of pixels changed
        bool hasFaceBrightness = brightnessScore > 50;  // Enough pixels in face brightness range
        
        if (hasMotion && hasFaceBrightness) {
            personDetected = true;
            lastFaceDetectionResult = true;
            lastFaceDetectionTime = millis();
            Serial.println("Person detected (motion + face-like brightness)!");
        } else {
            // Update detection result even if no face detected
            if (personDetected == false) {
                lastFaceDetectionResult = false;
            }
        }
    }
    
    // Return frame buffer
    esp_camera_fb_return(fb);
    
    return personDetected;
}

void checkReminders() {
    updateTime();
    
    int currentHour = rtcDateTime.time.hours;
    int currentMinute = rtcDateTime.time.minutes;
    
    // Day cycle resets at 4:00 AM (cycle runs from 4:00 AM to 3:59 AM)
    // Reset flags at 4:00 AM to start a new day cycle
    if (currentHour == 4 && currentMinute == 0) {
        reminderShown = false;
        reminderDismissed = false;
        Serial.println("Day cycle reset - reminder flags cleared");
    }
    
    // Check for face recognition trigger (only if not already dismissed in this cycle)
    unsigned long currentTime = millis();
    if (!reminderDismissed && (currentTime - lastFaceCheck > FACE_CHECK_INTERVAL)) {
        lastFaceCheck = currentTime;
        
        if (checkForFace()) {
            // Face detected - trigger reminder if not already shown
            if (!reminderShown && currentState == IDLE) {
                currentState = REMINDER_ACTIVE;
                reminderStartTime = millis();
                reminderShown = true;
                Serial.println("Face detected - reminder triggered!");
            }
        }
    }
    
    // Reminder stays active until dismissed (no auto-dismiss)
    // User must press a button to dismiss
}

void displayReminder() {
    M5.Display.fillScreen(BLACK);
    
    // Display camera preview in bottom right (before other content)
    if (cameraInitialized) {
        displayCameraPreview();
    }
    
    // Display WiFi status at upper right
    updateWiFiStatus();
    displayWiFiStatus();
    
    // Display camera status below WiFi
    displayCameraStatus();
    
    if (currentState == REMINDER_ACTIVE) {
        // Reminder display (240x320 portrait, rotated 90° right)
        M5.Display.setTextColor(YELLOW);
        M5.Display.setTextSize(3);
        M5.Display.setCursor(30, 50);
        M5.Display.println("REMINDER");
        
        M5.Display.setTextColor(WHITE);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(70, 120);
        M5.Display.println("Brush");
        M5.Display.setCursor(50, 150);
        M5.Display.println("Your Teeth!");
        
        // Draw tooth emoji
        M5.Display.fillCircle(120, 220, 20, WHITE);
        M5.Display.fillRect(100, 220, 40, 25, BLACK);
        
        // Play beep
        if (M5.Speaker.isEnabled()) {
            M5.Speaker.tone(1000, 200);
        }
    }
}

void displayIdle() {
    M5.Display.fillScreen(BLACK);
    
    // Display WiFi status at upper right
    updateWiFiStatus();
    displayWiFiStatus();
    
    // Display camera status below WiFi
    displayCameraStatus();
    
    // Display current time (240x320 portrait, rotated 90° right)
    updateTime();
    M5.Display.setTextColor(GREEN);
    M5.Display.setTextSize(4);
    M5.Display.setCursor(50, 80);
    
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", rtcDateTime.time.hours, rtcDateTime.time.minutes);
    M5.Display.println(timeStr);
    
    // Display date
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(60, 140);
    
    char dateStr[12];
    sprintf(dateStr, "%02d/%02d/%04d", rtcDateTime.date.date, rtcDateTime.date.month, rtcDateTime.date.year);
    M5.Display.println(dateStr);
    
    // Display reminder status
    M5.Display.setTextColor(YELLOW);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(50, 180);
    if (reminderDismissed) {
        M5.Display.println("Reminder: Done");
    } else if (reminderShown) {
        M5.Display.println("Reminder: Active");
    } else {
        M5.Display.println("Waiting: Face");
    }
    
    // Display status
    M5.Display.setTextColor(CYAN);
    M5.Display.setCursor(40, 220);
    M5.Display.println("TeethBrush Reminder");
    
    // Display camera preview in bottom right (will be updated in loop)
    if (cameraInitialized) {
        displayCameraPreview();
    }
}

void handleButtons() {
    M5.update();
    unsigned long currentTime = millis();
    
    // Button debounce
    if (currentTime - lastButtonPress < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    // Check button presses (CoreS3 has buttons A, B, C)
    bool buttonPressed = false;
    if (M5.BtnA.wasPressed()) {
        buttonPressed = true;
        Serial.println("Button A pressed");
    } else if (M5.BtnB.wasPressed()) {
        buttonPressed = true;
        Serial.println("Button B pressed");
    } else if (M5.BtnC.wasPressed()) {
        buttonPressed = true;
        Serial.println("Button C pressed");
    }
    
    if (buttonPressed) {
        lastButtonPress = currentTime;
        
        if (currentState == REMINDER_ACTIVE) {
            // Dismiss reminder - mark as dismissed for this cycle
            currentState = IDLE;
            reminderDismissed = true;
            reminderShown = true;  // Keep as shown so it won't trigger again this cycle
            Serial.println("Reminder dismissed - won't show again until next cycle");
            if (M5.Speaker.isEnabled()) {
                M5.Speaker.tone(800, 100);
            }
            // Return to idle display
            displayIdle();
        }
    }
}

void setup() {
    // Initialize M5Stack CoreS3
    // M5Unified will auto-detect CoreS3
    auto cfg = M5.config();
    M5.begin(cfg);
    
    // Initialize serial
    Serial.begin(115200);
    delay(500);  // Wait for serial to initialize
    Serial.println("TeethBrushReminder starting...");
    Serial.printf("Device: CoreS3\n");
    
    // Initialize RTC
    setupRTC();
    
    // Initialize camera for face recognition
    Serial.println("Initializing camera...");
    delay(500);  // Give system time to stabilize
    if (initCamera()) {
        Serial.println("Camera ready for face detection");
    } else {
        Serial.println("WARNING: Camera initialization failed - face recognition disabled");
        Serial.println("Note: CoreS3SE does not have a camera. If you have CoreS3SE, this is normal.");
    }
    
    // Initialize display
    if (M5.Display.width() > 0) {
        M5.Display.setRotation(1);  // Portrait mode, rotated 90° right (240x320)
        M5.Display.fillScreen(BLACK);
        M5.Display.setTextColor(WHITE);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(60, 120);
        M5.Display.println("TeethBrush");
        M5.Display.setCursor(70, 150);
        M5.Display.println("Reminder");
        M5.Display.setCursor(65, 180);
        M5.Display.println("Initializing...");
    }
    
    delay(1000);
    
    // Display idle screen
    displayIdle();
    
    Serial.println("Setup complete!");
}

void loop() {
    // Update buttons
    handleButtons();
    
    // Check for reminders
    checkReminders();
    
    // Update display based on state
    if (currentState == REMINDER_ACTIVE) {
        // Keep reminder displayed (stays until dismissed)
        displayReminder();
    } else {
        // Update idle display every minute
        static unsigned long lastIdleUpdate = 0;
        static int lastMinute = -1;
        updateTime();
        if (rtcDateTime.time.minutes != lastMinute) {
            displayIdle();
            lastMinute = rtcDateTime.time.minutes;
            lastIdleUpdate = millis();
        }
    }
    
    // Update camera preview periodically (every 100ms = 10 FPS)
    unsigned long currentTime = millis();
    if (cameraInitialized && (currentTime - lastCameraPreview > CAMERA_PREVIEW_INTERVAL)) {
        displayCameraPreview();
        lastCameraPreview = currentTime;
    }
    
    delay(100);  // Small delay for stability
}

