#include "M5CoreS3.h"
#include "esp_camera.h"
#include <algorithm>
#include <cstdint>

void setup() {
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    Serial.begin(115200);
    delay(1000);  // Give more time for serial to initialize
    Serial.println("Camera Display - Starting...");
    
    // Initialize display
    CoreS3.Display.setRotation(1);  // Portrait mode, rotated 90° right (240x320)
    CoreS3.Display.fillScreen(BLACK);
    CoreS3.Display.setTextColor(WHITE);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setCursor(50, 150);
    CoreS3.Display.println("Initializing...");
    
    delay(1000);  // Give system time to stabilize
    
    Serial.println("Attempting to initialize camera...");
    
    // Initialize camera using M5Stack's built-in camera support
    if (!CoreS3.Camera.begin()) {
        Serial.println("Camera Init Fail");
        CoreS3.Display.fillScreen(BLACK);
        CoreS3.Display.setCursor(30, 150);
        CoreS3.Display.setTextColor(RED);
        CoreS3.Display.println("Camera FAILED!");
        Serial.println("Camera initialization failed. Check hardware connections.");
        while(1) delay(1000);  // Stop here if camera fails
    }
    
    Serial.println("Camera Init Success");
    
    // Set camera frame size
    if (CoreS3.Camera.sensor) {
        CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
    }
    
    // Display success message
    CoreS3.Display.fillScreen(BLACK);
    CoreS3.Display.setCursor(50, 150);
    CoreS3.Display.setTextColor(GREEN);
    CoreS3.Display.println("Camera OK!");
    delay(500);
    
    // Clear the display area where camera will be shown
    CoreS3.Display.fillScreen(BLACK);
}

constexpr float VIDEO_SCALE = 0.5f;
constexpr int BUTTON_WIDTH = 70;
constexpr int BUTTON_RADIUS = 10;
constexpr int BUTTON_MARGIN = 8;
constexpr int STATUS_HEIGHT = 22;
constexpr float MIN_FACE_BRIGHTNESS = 60.0f;
constexpr float MAX_FACE_BRIGHTNESS = 200.0f;
constexpr float MIN_SKIN_RATIO = 0.18f;

bool detectFaceLikeFeatures(const uint16_t* frame, int width, int height) {
    if (!frame || width <= 0 || height <= 0) {
        return false;
    }

    const int startX = width / 4;
    const int endX = width - startX;
    const int startY = height / 4;
    const int endY = height - startY;

    uint32_t sampleCount = 0;
    uint32_t skinCount = 0;
    uint32_t brightnessSum = 0;

    for (int y = startY; y < endY; y += 2) {
        const int rowOffset = y * width;
        for (int x = startX; x < endX; x += 2) {
            const uint16_t pixel = frame[rowOffset + x];
            const uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            const uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            const uint8_t b = (pixel & 0x1F) << 3;

            const uint16_t brightness = (r * 30 + g * 59 + b * 11) / 100;
            brightnessSum += brightness;

            if (r > g && g >= b / 2 && r - g < 80 && brightness > 80) {
                skinCount++;
            }
            sampleCount++;
        }
    }

    if (sampleCount == 0) {
        return false;
    }

    const float avgBrightness = static_cast<float>(brightnessSum) / sampleCount;
    const float skinRatio = static_cast<float>(skinCount) / sampleCount;

    return (avgBrightness >= MIN_FACE_BRIGHTNESS &&
            avgBrightness <= MAX_FACE_BRIGHTNESS &&
            skinRatio >= MIN_SKIN_RATIO);
}

void drawFaceStatus(bool faceDetected) {
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), STATUS_HEIGHT, BLACK);
    CoreS3.Display.setCursor(8, 4);
    CoreS3.Display.setTextSize(2);
    if (faceDetected) {
        CoreS3.Display.setTextColor(GREEN, BLACK);
        CoreS3.Display.print("Face detected");
    } else {
        CoreS3.Display.setTextColor(RED, BLACK);
        CoreS3.Display.print("No face");
    }
}

void drawButton(int x, int y, int height, const char* label, uint16_t color) {
    CoreS3.Display.fillRoundRect(x, y, BUTTON_WIDTH, height, BUTTON_RADIUS, color);
    CoreS3.Display.drawRoundRect(x, y, BUTTON_WIDTH, height, BUTTON_RADIUS, WHITE);

    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, color);
    const int textWidth = CoreS3.Display.textWidth(label);
    const int textHeight = CoreS3.Display.fontHeight();
    const int textX = x + (BUTTON_WIDTH - textWidth) / 2;
    const int textY = y + (height - textHeight) / 2;
    CoreS3.Display.setCursor(textX, textY);
    CoreS3.Display.print(label);
}

void loop() {
    if (CoreS3.Camera.get()) {
        const int frameWidth = CoreS3.Camera.fb->width;
        const int frameHeight = CoreS3.Camera.fb->height;

        const int targetWidth = static_cast<int>(frameWidth * VIDEO_SCALE);
        const int targetHeight = static_cast<int>(frameHeight * VIDEO_SCALE);

        const int displayWidth = CoreS3.Display.width();
        const int displayHeight = CoreS3.Display.height();

        const int videoTop = std::max(0, displayHeight - targetHeight);

        const float centerX = displayWidth * 0.5f;
        const float centerY = videoTop + targetHeight * 0.5f;
        const float sourceCenterX = frameWidth * 0.5f;
        const float sourceCenterY = frameHeight * 0.5f;

        const bool faceDetected = detectFaceLikeFeatures(
            reinterpret_cast<uint16_t*>(CoreS3.Camera.fb->buf),
            frameWidth,
            frameHeight);

        drawFaceStatus(faceDetected);

        CoreS3.Display.pushImageRotateZoom(centerX, centerY,
                                           sourceCenterX, sourceCenterY,
                                           0.0f, VIDEO_SCALE, VIDEO_SCALE,
                                           frameWidth, frameHeight,
                                           (uint16_t *)CoreS3.Camera.fb->buf);
        
        const int buttonHeight = targetHeight;
        const int buttonTop = videoTop;
        drawButton(BUTTON_MARGIN, buttonTop, buttonHeight, "Yes", GREEN);
        drawButton(displayWidth - BUTTON_WIDTH - BUTTON_MARGIN, buttonTop, buttonHeight, "No", RED);

        CoreS3.Camera.free();
    }
    delay(50);  // ~20 FPS
}
