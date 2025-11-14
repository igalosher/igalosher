#include "M5CoreS3.h"
#include "esp_camera.h"
#include <SPI.h>
#include <SD.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <vector>

struct Rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    bool contains(int16_t px, int16_t py) const {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

Rect makeRect(int x, int y, int w, int h) {
    return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y),
                static_cast<int16_t>(w), static_cast<int16_t>(h)};
}

struct FaceProfile {
    String id;
    String name;
    std::vector<uint8_t> descriptor;
};

struct MenuItem {
    Rect rect;
    String action;
    String value;
};

enum class MenuState {
    Hidden,
    Main,
    EnrollName,
    EnrollCapture,
    ManageList,
    ManageDetail
};

constexpr float VIDEO_SCALE = 0.5f;
constexpr int BUTTON_WIDTH = 70;
constexpr int BUTTON_RADIUS = 10;
constexpr int BUTTON_MARGIN = 8;
constexpr int STATUS_HEIGHT = 22;
constexpr int DATETIME_HEIGHT = 22;
constexpr float MIN_FACE_BRIGHTNESS = 60.0f;
constexpr float MAX_FACE_BRIGHTNESS = 200.0f;
constexpr float MIN_SKIN_RATIO = 0.18f;
constexpr int TIMEZONE_OFFSET_MINUTES = 120;  // Israel GMT+2
constexpr int DESCRIPTOR_SIZE = 32;
constexpr float MATCH_THRESHOLD = 18.0f;
constexpr unsigned long RECOGNITION_HOLD_MS = 3000;

constexpr uint16_t COLOR_ACCENT = 0x03EF;
constexpr uint16_t COLOR_MUTED = 0x5ACB;
constexpr uint16_t COLOR_SUCCESS = 0x07E0;

constexpr int SD_SPI_SCK_PIN = 36;
constexpr int SD_SPI_MISO_PIN = 35;
constexpr int SD_SPI_MOSI_PIN = 37;
constexpr int SD_SPI_CS_PIN = 4;

constexpr char FACES_DIR[] = "/faces";

std::vector<FaceProfile> g_profiles;
bool g_sdAttempted = false;
bool g_sdReady = false;

MenuState g_menuState = MenuState::Hidden;
std::vector<MenuItem> g_menuItems;

String g_inputBuffer;
String g_statusMessage;
unsigned long g_statusMessageUntil = 0;

String g_pendingName;
std::vector<uint8_t> g_pendingDescriptor;
String g_editingProfileId;
bool g_editingExisting = false;
bool g_isRenaming = false;
bool g_isRecapturing = false;
int g_manageScroll = 0;
int g_selectedProfileIndex = -1;

std::vector<uint8_t> g_latestDescriptor;
unsigned long g_lastDescriptorTime = 0;
String g_lastMatchName;
unsigned long g_lastMatchTimestamp = 0;
bool g_lastFaceDetected = false;
bool g_captureFaceDetected = false;
int g_captureVideoBottom = 0;

void changeMenuState(MenuState newState) {
    if (g_menuState == newState) {
        return;
    }
    g_menuState = newState;
    CoreS3.Display.fillScreen(BLACK);
}

bool detectFaceLikeFeatures(const uint16_t* frame, int width, int height);
tm applyTimezoneOffset(const m5::rtc_datetime_t& dt);
void drawFaceStatus(bool faceDetected);
void drawDateTimeRow();
void drawButton(int x, int y, int height, const char* label, uint16_t color);
Rect menuButtonRect();
void drawMenuButton();
void showStatusMessage(const String& message, uint32_t durationMs = 2000);
bool ensureSdCard();
String makeProfilePath(const String& id);
bool saveProfileToSd(const FaceProfile& profile);
bool deleteProfileFromSd(const String& id);
void loadProfiles();
FaceProfile* findProfileById(const String& id);
std::vector<uint8_t> extractFaceDescriptor(const uint16_t* frame, int width, int height);
float descriptorDistance(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
void updateRecognition(const std::vector<uint8_t>& descriptor);
void beginEnrollFlow(bool editingExisting, bool renaming, bool recapturing);
void completeNameEntry();
void savePendingProfile();
void refreshProfilesFromDisk();
void addMenuButtonInternal(const Rect& rect, const String& label, uint16_t color,
                           const String& action, const String& value = "");
void drawStatusMessageArea(const Rect& panel);
void drawMainMenu(const Rect& panel);
void drawNameEntry(const Rect& panel);
void drawCapturePanel(const Rect& panel);
void drawManageList(const Rect& panel);
void drawManageDetail(const Rect& panel);
void drawMenuOverlay();
void dispatchMenuAction(const String& action, const String& value);
void handleTap(int16_t x, int16_t y);
void changeMenuState(MenuState newState);

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

tm applyTimezoneOffset(const m5::rtc_datetime_t& dt) {
    tm utc{};
    utc.tm_year = dt.date.year - 1900;
    utc.tm_mon = dt.date.month - 1;
    utc.tm_mday = dt.date.date;
    utc.tm_hour = dt.time.hours;
    utc.tm_min = dt.time.minutes;
    utc.tm_sec = dt.time.seconds;
    utc.tm_isdst = -1;

    time_t epoch = mktime(&utc);
    epoch += TIMEZONE_OFFSET_MINUTES * 60;

    tm local{};
#if defined(_WIN32) || defined(_WIN64)
    gmtime_s(&local, &epoch);
#else
    gmtime_r(&epoch, &local);
#endif
    return local;
}

void drawFaceStatus(bool faceDetected) {
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), STATUS_HEIGHT, BLACK);
    CoreS3.Display.setCursor(8, 4);
    CoreS3.Display.setTextSize(2);
    if (faceDetected) {
        CoreS3.Display.setTextColor(GREEN, BLACK);
        String label = "Face: ";
        label += g_lastMatchName.isEmpty() ? "Unknown" : g_lastMatchName;
        CoreS3.Display.print(label);
    } else {
        CoreS3.Display.setTextColor(RED, BLACK);
        CoreS3.Display.print("No face");
    }
}

void drawDateTimeRow() {
    const auto dt = CoreS3.Rtc.getDateTime();
    const tm localTm = applyTimezoneOffset(dt);

    CoreS3.Display.fillRect(0, STATUS_HEIGHT, CoreS3.Display.width(), DATETIME_HEIGHT, BLACK);
    CoreS3.Display.setTextColor(WHITE, BLACK);
    CoreS3.Display.setTextSize(2);

    char timeBuffer[16];
    std::snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
                  localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
    CoreS3.Display.setCursor(8, STATUS_HEIGHT + 2);
    CoreS3.Display.print(timeBuffer);

    char dateBuffer[20];
    std::snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d",
                  localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
    const int textWidth = std::max(0, CoreS3.Display.textWidth(dateBuffer));
    const int dateX = std::max(8, CoreS3.Display.width() - textWidth - 8);
    CoreS3.Display.setCursor(dateX, STATUS_HEIGHT + 2);
    CoreS3.Display.print(dateBuffer);
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

Rect menuButtonRect() {
    const int y = STATUS_HEIGHT + DATETIME_HEIGHT + 4;
    return makeRect(CoreS3.Display.width() - 90, y, 70, DATETIME_HEIGHT);
}

void drawMenuButton() {
    if (g_menuState != MenuState::Hidden) {
        return;
    }
    const Rect rect = menuButtonRect();
    CoreS3.Display.fillRect(rect.x, rect.y, rect.w, rect.h, COLOR_ACCENT);
    CoreS3.Display.drawRect(rect.x, rect.y, rect.w, rect.h, WHITE);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, COLOR_ACCENT);
    CoreS3.Display.setCursor(rect.x + 8, rect.y + 4);
    CoreS3.Display.print("Menu");
}

void showStatusMessage(const String& message, uint32_t durationMs) {
    g_statusMessage = message;
    g_statusMessageUntil = millis() + durationMs;
}

bool ensureSdCard() {
    if (g_sdReady) {
        return true;
    }
    if (!g_sdAttempted) {
        g_sdAttempted = true;
        SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
        g_sdReady = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
        if (g_sdReady && !SD.exists(FACES_DIR)) {
            SD.mkdir(FACES_DIR);
        }
        if (!g_sdReady) {
            showStatusMessage("SD card missing", 3000);
        }
    }
    return g_sdReady;
}

String makeProfilePath(const String& id) {
    return String(FACES_DIR) + "/" + id + ".bin";
}

bool saveProfileToSd(const FaceProfile& profile) {
    if (!ensureSdCard()) {
        return false;
    }
    if (!SD.exists(FACES_DIR)) {
        SD.mkdir(FACES_DIR);
    }
    File file = SD.open(makeProfilePath(profile.id), FILE_WRITE);
    if (!file) {
        return false;
    }
    const uint16_t nameLen = profile.name.length();
    const uint16_t descriptorLen = profile.descriptor.size();
    file.write(reinterpret_cast<const uint8_t*>(&nameLen), sizeof(nameLen));
    file.write(reinterpret_cast<const uint8_t*>(profile.name.c_str()), nameLen);
    file.write(reinterpret_cast<const uint8_t*>(&descriptorLen), sizeof(descriptorLen));
    if (descriptorLen > 0) {
        file.write(profile.descriptor.data(), descriptorLen);
    }
    file.close();
    return true;
}

bool deleteProfileFromSd(const String& id) {
    if (!ensureSdCard()) {
        return false;
    }
    return SD.remove(makeProfilePath(id));
}

void loadProfiles() {
    g_profiles.clear();
    if (!ensureSdCard()) {
        return;
    }
    if (!SD.exists(FACES_DIR)) {
        SD.mkdir(FACES_DIR);
    }
    File dir = SD.open(FACES_DIR);
    if (!dir) {
        return;
    }
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            FaceProfile profile;
            String fileName = String(file.name());
            const int slash = fileName.lastIndexOf('/');
            if (slash >= 0) {
                fileName = fileName.substring(slash + 1);
            }
            if (fileName.endsWith(".bin")) {
                fileName.remove(fileName.length() - 4);
            }
            profile.id = fileName;

            uint16_t nameLen = 0;
            uint16_t descriptorLen = 0;
            file.read(reinterpret_cast<uint8_t*>(&nameLen), sizeof(nameLen));
            if (nameLen > 0) {
                std::vector<char> nameBuffer(nameLen + 1, 0);
                file.read(reinterpret_cast<uint8_t*>(nameBuffer.data()), nameLen);
                profile.name = String(nameBuffer.data());
            } else {
                profile.name = "Unnamed";
            }
            file.read(reinterpret_cast<uint8_t*>(&descriptorLen), sizeof(descriptorLen));
            if (descriptorLen > 0) {
                profile.descriptor.resize(descriptorLen);
                file.read(profile.descriptor.data(), descriptorLen);
            }
            g_profiles.push_back(profile);
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
}

FaceProfile* findProfileById(const String& id) {
    for (auto& profile : g_profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

std::vector<uint8_t> extractFaceDescriptor(const uint16_t* frame, int width, int height) {
    std::vector<uint8_t> descriptor(DESCRIPTOR_SIZE * DESCRIPTOR_SIZE);
    if (!frame || width <= 0 || height <= 0) {
        descriptor.clear();
        return descriptor;
    }
    const int cropSize = std::min(width, height) / 2;
    if (cropSize <= 0) {
        descriptor.clear();
        return descriptor;
    }
    const int startX = (width - cropSize) / 2;
    const int startY = (height - cropSize) / 2;

    for (int y = 0; y < DESCRIPTOR_SIZE; ++y) {
        int srcY = startY + (y * cropSize) / DESCRIPTOR_SIZE;
        srcY = std::min(std::max(srcY, 0), height - 1);
        for (int x = 0; x < DESCRIPTOR_SIZE; ++x) {
            int srcX = startX + (x * cropSize) / DESCRIPTOR_SIZE;
            srcX = std::min(std::max(srcX, 0), width - 1);
            const uint16_t pixel = frame[srcY * width + srcX];
            const uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            const uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            const uint8_t b = (pixel & 0x1F) << 3;
            const uint8_t gray = static_cast<uint8_t>((r * 30 + g * 59 + b * 11) / 100);
            descriptor[y * DESCRIPTOR_SIZE + x] = gray;
        }
    }
    return descriptor;
}

float descriptorDistance(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) {
        return std::numeric_limits<float>::max();
    }
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        const float diff = static_cast<float>(a[i]) - static_cast<float>(b[i]);
        sum += diff * diff;
    }
    return sqrtf(sum / a.size());
}

void updateRecognition(const std::vector<uint8_t>& descriptor) {
    if (descriptor.empty() || g_profiles.empty()) {
        if (millis() > g_lastMatchTimestamp + RECOGNITION_HOLD_MS) {
            g_lastMatchName = "";
        }
        return;
    }

    float bestScore = std::numeric_limits<float>::max();
    String bestName;
    for (const auto& profile : g_profiles) {
        if (profile.descriptor.empty()) {
            continue;
        }
        const float score = descriptorDistance(descriptor, profile.descriptor);
        if (score < bestScore) {
            bestScore = score;
            bestName = profile.name;
        }
    }

    if (bestScore < MATCH_THRESHOLD) {
        g_lastMatchName = bestName;
        g_lastMatchTimestamp = millis();
    } else if (millis() > g_lastMatchTimestamp + RECOGNITION_HOLD_MS) {
        g_lastMatchName = "";
    }
}

void beginEnrollFlow(bool editingExisting, bool renaming, bool recapturing) {
    g_editingExisting = editingExisting;
    g_isRenaming = renaming;
    g_isRecapturing = recapturing;
    if (!editingExisting) {
        g_editingProfileId = "";
        g_pendingName = "";
    }
    g_pendingDescriptor.clear();
    g_inputBuffer = renaming ? g_pendingName : "";
    MenuState target = renaming ? MenuState::EnrollName :
                       (recapturing ? MenuState::EnrollCapture : MenuState::EnrollName);
    changeMenuState(target);
}

void completeNameEntry() {
    if (g_inputBuffer.isEmpty()) {
        showStatusMessage("Enter a name", 1500);
        return;
    }
    if (g_isRenaming && g_editingExisting) {
        auto* profile = findProfileById(g_editingProfileId);
        if (profile) {
            profile->name = g_inputBuffer;
            saveProfileToSd(*profile);
            showStatusMessage("Name updated", 1500);
        }
        changeMenuState(MenuState::ManageDetail);
        return;
    }
    g_pendingName = g_inputBuffer;
    changeMenuState(MenuState::EnrollCapture);
}

void savePendingProfile() {
    if (g_pendingDescriptor.empty()) {
        showStatusMessage("Capture a face first", 1500);
        return;
    }
    if (g_pendingName.isEmpty()) {
        showStatusMessage("Name missing", 1500);
        return;
    }

    if (g_editingExisting) {
        auto* profile = findProfileById(g_editingProfileId);
        if (profile) {
            profile->descriptor = g_pendingDescriptor;
            profile->name = g_pendingName;
            saveProfileToSd(*profile);
            showStatusMessage("Profile updated", 1500);
        }
    } else {
        FaceProfile profile;
        profile.id = String(millis());
        profile.name = g_pendingName;
        profile.descriptor = g_pendingDescriptor;
        g_profiles.push_back(profile);
        saveProfileToSd(g_profiles.back());
        showStatusMessage("Person saved", 1500);
    }
    changeMenuState(MenuState::Main);
}

void refreshProfilesFromDisk() {
    loadProfiles();
    g_manageScroll = 0;
    g_selectedProfileIndex = -1;
}

void addMenuButtonInternal(const Rect& rect, const String& label, uint16_t color,
                           const String& action, const String& value) {
    CoreS3.Display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, color);
    CoreS3.Display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, WHITE);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, color);
    const int textWidth = CoreS3.Display.textWidth(label);
    CoreS3.Display.setCursor(rect.x + (rect.w - textWidth) / 2,
                             rect.y + rect.h / 2 - CoreS3.Display.fontHeight() / 2);
    CoreS3.Display.print(label);
    g_menuItems.push_back(MenuItem{rect, action, value});
}

void drawStatusMessageArea(const Rect& panel) {
    if (g_statusMessage.isEmpty() || millis() > g_statusMessageUntil) {
        return;
    }
    const int padding = 8;
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(YELLOW, BLACK);
    CoreS3.Display.setCursor(panel.x + padding, panel.y + panel.h - 30);
    CoreS3.Display.print(g_statusMessage);
}

void drawMainMenu(const Rect& panel) {
    const int padding = 12;
    const int btnHeight = 40;
    int y = panel.y + padding;
    Rect enroll = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(enroll, "Enroll Person", COLOR_ACCENT, "open_enroll");
    y += btnHeight + padding;
    Rect manage = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(manage, "Manage People", COLOR_ACCENT, "open_manage");
    y += btnHeight + padding;
    Rect closeBtn = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(closeBtn, "Close", COLOR_MUTED, "close_menu");
}

void drawNameEntry(const Rect& panel) {
    const int padding = 6;
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, BLACK);
    CoreS3.Display.setCursor(panel.x + padding, panel.y + padding);
    CoreS3.Display.print("Name: ");
    CoreS3.Display.print(g_inputBuffer);

    const char* rows[] = {"ABCDEFGH", "IJKLMNOP", "QRSTUV", "WXYZ"};
    const int btnW = 30;
    const int btnH = 30;
    int y = panel.y + padding + 24;
    for (const char* row : rows) {
        const int len = strlen(row);
        int x = panel.x + padding;
        for (int i = 0; i < len; ++i) {
            Rect r = makeRect(x, y, btnW, btnH);
            String letter(row[i]);
            addMenuButtonInternal(r, letter, COLOR_SUCCESS, "letter", letter);
            x += btnW + 5;
        }
        y += btnH + 5;
    }
    Rect spaceBtn = makeRect(panel.x + padding, y, 70, 30);
    Rect backspaceBtn = makeRect(spaceBtn.x + spaceBtn.w + 5, y, 70, 30);
    Rect clearBtn = makeRect(backspaceBtn.x + backspaceBtn.w + 5, y, 70, 30);
    Rect kbBackBtn = makeRect(clearBtn.x + clearBtn.w + 5, y, 60, 30);
    addMenuButtonInternal(spaceBtn, "Space", COLOR_ACCENT, "space");
    addMenuButtonInternal(backspaceBtn, "Del", COLOR_MUTED, "backspace");
    addMenuButtonInternal(clearBtn, "Clear", COLOR_MUTED, "clear");
    addMenuButtonInternal(kbBackBtn, "Back", COLOR_MUTED, "menu_back");
    y += 34;
    Rect nextBtn = makeRect(panel.x + padding, y, panel.w - 2 * padding, 36);
    addMenuButtonInternal(nextBtn, g_isRenaming ? "Save Name" : "Next", COLOR_ACCENT, "confirm_name");
}

void drawCapturePanel(const Rect& panel) {
    const int padding = 8;
    const int controlsTop = (g_captureVideoBottom > 0 ? g_captureVideoBottom + 40
                                                     : CoreS3.Display.height() / 2 + 20);

    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, BLACK);
    CoreS3.Display.fillRect(panel.x + padding, controlsTop - 40, panel.w - 2 * padding, 36, BLACK);
    CoreS3.Display.setCursor(panel.x + padding, controlsTop - 38);
    CoreS3.Display.print(g_captureFaceDetected ? "Face detected" : "No face detected");
    CoreS3.Display.setCursor(panel.x + padding, controlsTop - 20);
    CoreS3.Display.print("Align face then tap Capture");

    int y = controlsTop;
    Rect captureBtn = makeRect(panel.x + padding, y, panel.w - 2 * padding, 38);
    addMenuButtonInternal(captureBtn, "Capture", COLOR_SUCCESS, "capture_face");
    y += 46;
    Rect saveBtn = makeRect(panel.x + padding, y, (panel.w - 3 * padding) / 2, 38);
    Rect backBtn = makeRect(saveBtn.x + saveBtn.w + padding, y, (panel.w - 3 * padding) / 2, 38);
    addMenuButtonInternal(saveBtn, "Save", COLOR_ACCENT, "save_profile");
    addMenuButtonInternal(backBtn, "Back", COLOR_MUTED, "cancel_capture");
}

void drawManageList(const Rect& panel) {
    const int padding = 10;
    const int btnHeight = 34;
    const int visible = 5;
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, BLACK);
    CoreS3.Display.setCursor(panel.x + padding, panel.y + padding);
    CoreS3.Display.print("People:");

    int y = panel.y + padding + 20;
    const int total = static_cast<int>(g_profiles.size());
    g_manageScroll = std::max(0, std::min(g_manageScroll, std::max(0, total - visible)));
    for (int i = 0; i < visible; ++i) {
        const int profileIndex = g_manageScroll + i;
        if (profileIndex >= total) {
            break;
        }
        const auto& profile = g_profiles[profileIndex];
        Rect row = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
        addMenuButtonInternal(row, profile.name, COLOR_ACCENT, "select_profile", String(profileIndex));
        y += btnHeight + 6;
    }

    Rect scrollUp = makeRect(panel.x + padding, panel.y + panel.h - 100, 60, 32);
    Rect scrollDown = makeRect(scrollUp.x + scrollUp.w + 6, scrollUp.y, 60, 32);
    Rect backBtn = makeRect(scrollDown.x + scrollDown.w + 6, scrollUp.y,
                            panel.w - (scrollDown.x + scrollDown.w + 6) - padding, 32);
    addMenuButtonInternal(scrollUp, "Up", COLOR_MUTED, "scroll_up");
    addMenuButtonInternal(scrollDown, "Down", COLOR_MUTED, "scroll_down");
    addMenuButtonInternal(backBtn, "Back", COLOR_MUTED, "menu_back");
}

void drawManageDetail(const Rect& panel) {
    if (g_selectedProfileIndex < 0 || g_selectedProfileIndex >= static_cast<int>(g_profiles.size())) {
        changeMenuState(MenuState::ManageList);
        return;
    }
    const auto& profile = g_profiles[g_selectedProfileIndex];
    const int padding = 10;
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(WHITE, BLACK);
    CoreS3.Display.setCursor(panel.x + padding, panel.y + padding);
    CoreS3.Display.print("Editing:");
    CoreS3.Display.setCursor(panel.x + padding, panel.y + padding + 24);
    CoreS3.Display.print(profile.name);

    int y = panel.y + padding + 60;
    const int btnHeight = 36;
    Rect rename = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(rename, "Rename", COLOR_ACCENT, "rename_profile");
    y += btnHeight + 8;
    Rect recapture = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(recapture, "Re-Capture Face", COLOR_ACCENT, "recapture_profile");
    y += btnHeight + 8;
    Rect deleteBtn = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(deleteBtn, "Delete", RED, "delete_profile");
    y += btnHeight + 8;
    Rect backBtn = makeRect(panel.x + padding, y, panel.w - 2 * padding, btnHeight);
    addMenuButtonInternal(backBtn, "Back", COLOR_MUTED, "menu_back");
}

void drawMenuOverlay() {
    static MenuState lastState = MenuState::Hidden;
    if (g_menuState == MenuState::Hidden) {
        g_menuItems.clear();
        lastState = MenuState::Hidden;
        return;
    }

    if (lastState != g_menuState) {
        CoreS3.Display.fillScreen(BLACK);
        lastState = g_menuState;
    }

    const Rect panel = makeRect(0, 0, CoreS3.Display.width(), CoreS3.Display.height());
    g_menuItems.clear();

    if (g_menuState == MenuState::EnrollCapture) {
        drawCapturePanel(panel);
        return;
    }

    CoreS3.Display.drawRoundRect(panel.x + 4, panel.y + 4, panel.w - 8, panel.h - 8, 12, WHITE);

    switch (g_menuState) {
        case MenuState::Main:
            drawMainMenu(panel);
            break;
        case MenuState::EnrollName:
            drawNameEntry(panel);
            break;
        case MenuState::ManageList:
            drawManageList(panel);
            break;
        case MenuState::ManageDetail:
            drawManageDetail(panel);
            break;
        default:
            break;
    }

    drawStatusMessageArea(panel);
}

void dispatchMenuAction(const String& action, const String& value) {
    if (action == "close_menu") {
        changeMenuState(MenuState::Hidden);
        return;
    }
    if (action == "open_enroll") {
        g_pendingName = "";
        beginEnrollFlow(false, false, false);
        return;
    }
    if (action == "open_manage") {
        refreshProfilesFromDisk();
        changeMenuState(MenuState::ManageList);
        return;
    }
    if (action == "menu_back") {
        if (g_menuState == MenuState::ManageDetail) {
            changeMenuState(MenuState::ManageList);
        } else {
            changeMenuState(MenuState::Main);
        }
        return;
    }
    if (action == "letter") {
        if (value.length() == 1) {
            g_inputBuffer += value;
        }
        return;
    }
    if (action == "space") {
        g_inputBuffer += " ";
        return;
    }
    if (action == "backspace") {
        if (!g_inputBuffer.isEmpty()) {
            g_inputBuffer.remove(g_inputBuffer.length() - 1);
        }
        return;
    }
    if (action == "clear") {
        g_inputBuffer = "";
        return;
    }
    if (action == "confirm_name") {
        completeNameEntry();
        return;
    }
    if (action == "capture_face") {
        if (g_latestDescriptor.empty()) {
            showStatusMessage("No face detected", 1500);
        } else {
            g_pendingDescriptor = g_latestDescriptor;
            showStatusMessage("Face captured", 1500);
        }
        return;
    }
    if (action == "save_profile") {
        savePendingProfile();
        refreshProfilesFromDisk();
        return;
    }
    if (action == "cancel_capture") {
        changeMenuState(MenuState::Main);
        return;
    }
    if (action == "scroll_up") {
        g_manageScroll = std::max(0, g_manageScroll - 1);
        return;
    }
    if (action == "scroll_down") {
        const int maxScroll = std::max(0, static_cast<int>(g_profiles.size()) - 5);
        g_manageScroll = std::min(maxScroll, g_manageScroll + 1);
        return;
    }
    if (action == "select_profile") {
        const int index = value.toInt();
        if (index >= 0 && index < static_cast<int>(g_profiles.size())) {
            g_selectedProfileIndex = index;
            const auto& profile = g_profiles[index];
            g_pendingName = profile.name;
            g_editingProfileId = profile.id;
            changeMenuState(MenuState::ManageDetail);
        }
        return;
    }
    if (action == "rename_profile") {
        if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < static_cast<int>(g_profiles.size())) {
            const auto& profile = g_profiles[g_selectedProfileIndex];
            g_pendingName = profile.name;
            g_editingProfileId = profile.id;
            g_inputBuffer = profile.name;
            beginEnrollFlow(true, true, false);
        }
        return;
    }
    if (action == "recapture_profile") {
        if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < static_cast<int>(g_profiles.size())) {
            const auto& profile = g_profiles[g_selectedProfileIndex];
            g_pendingName = profile.name;
            g_editingProfileId = profile.id;
            beginEnrollFlow(true, false, true);
            changeMenuState(MenuState::EnrollCapture);
        }
        return;
    }
    if (action == "delete_profile") {
        if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < static_cast<int>(g_profiles.size())) {
            const auto profile = g_profiles[g_selectedProfileIndex];
            deleteProfileFromSd(profile.id);
            g_profiles.erase(g_profiles.begin() + g_selectedProfileIndex);
            showStatusMessage("Profile deleted", 1500);
            g_selectedProfileIndex = -1;
            changeMenuState(MenuState::ManageList);
        }
        return;
    }
}

void handleTap(int16_t x, int16_t y) {
    const Rect menuRect = menuButtonRect();
    if (g_menuState == MenuState::Hidden && menuRect.contains(x, y)) {
        changeMenuState(MenuState::Main);
        return;
    }

    if (g_menuState == MenuState::Hidden) {
        return;
    }

    for (const auto& item : g_menuItems) {
        if (item.rect.contains(x, y)) {
            dispatchMenuAction(item.action, item.value);
            return;
        }
    }

    (void)x;
    (void)y;
}

void setup() {
    auto cfg = M5.config();
    CoreS3.begin(cfg);

    Serial.begin(115200);
    delay(500);
    Serial.println("Camera Display - Starting...");

    CoreS3.Display.setRotation(1);
    CoreS3.Display.fillScreen(BLACK);
    CoreS3.Display.setTextColor(WHITE);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setCursor(30, 140);
    CoreS3.Display.println("Initializing...");

    delay(500);
    Serial.println("Attempting to initialize camera...");
    if (!CoreS3.Camera.begin()) {
        Serial.println("Camera Init Fail");
        CoreS3.Display.fillScreen(BLACK);
        CoreS3.Display.setCursor(30, 150);
        CoreS3.Display.setTextColor(RED);
        CoreS3.Display.println("Camera FAILED!");
        while (true) {
            delay(1000);
        }
    }
    Serial.println("Camera Init Success");

    if (CoreS3.Camera.sensor) {
        CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
    }

    CoreS3.Display.fillScreen(BLACK);
    ensureSdCard();
    loadProfiles();
}

void loop() {
    CoreS3.update();
    auto touchDetail = CoreS3.Touch.getDetail();
    const bool tapped = touchDetail.wasClicked();

    const bool isCaptureView = (g_menuState == MenuState::EnrollCapture);
    const bool wantsCamera = (g_menuState == MenuState::Hidden) || isCaptureView;

    if (wantsCamera && CoreS3.Camera.get()) {
        const int frameWidth = CoreS3.Camera.fb->width;
        const int frameHeight = CoreS3.Camera.fb->height;
        float scale = VIDEO_SCALE;
        if (isCaptureView) {
            const float maxHeight = CoreS3.Display.height() * 0.45f;
            scale = std::min(scale, maxHeight / frameHeight);
        }
        const int targetHeight = static_cast<int>(frameHeight * scale);

        const int displayWidth = CoreS3.Display.width();
        const int displayHeight = CoreS3.Display.height();
        const int videoTop = isCaptureView ? 10 : std::max(STATUS_HEIGHT + DATETIME_HEIGHT,
                                                           displayHeight - targetHeight);

        const float centerX = displayWidth * 0.5f;
        const float centerY = videoTop + targetHeight * 0.5f;
        const float sourceCenterX = frameWidth * 0.5f;
        const float sourceCenterY = frameHeight * 0.5f;

        const bool faceDetected = detectFaceLikeFeatures(
            reinterpret_cast<uint16_t*>(CoreS3.Camera.fb->buf),
            frameWidth,
            frameHeight);

        if (faceDetected) {
            g_latestDescriptor =
                extractFaceDescriptor(reinterpret_cast<uint16_t*>(CoreS3.Camera.fb->buf),
                                      frameWidth, frameHeight);
            g_lastDescriptorTime = millis();
            updateRecognition(g_latestDescriptor);
        } else if (millis() > g_lastDescriptorTime + 1500) {
            g_latestDescriptor.clear();
        }

        g_lastFaceDetected = faceDetected;

        CoreS3.Display.pushImageRotateZoom(centerX, centerY,
                                           sourceCenterX, sourceCenterY,
                                           0.0f, VIDEO_SCALE, VIDEO_SCALE,
                                           frameWidth, frameHeight,
                                           reinterpret_cast<uint16_t*>(CoreS3.Camera.fb->buf));

        if (isCaptureView) {
            g_captureVideoBottom = videoTop + targetHeight;
            g_captureFaceDetected = faceDetected;
        }

        const int buttonHeight = targetHeight;
        if (g_menuState == MenuState::Hidden) {
            drawButton(BUTTON_MARGIN, videoTop, buttonHeight, "Yes", GREEN);
            drawButton(displayWidth - BUTTON_WIDTH - BUTTON_MARGIN, videoTop, buttonHeight, "No", RED);
        }

        CoreS3.Camera.free();
    }

    if (g_menuState == MenuState::Hidden) {
        drawFaceStatus(g_lastFaceDetected);
        drawDateTimeRow();
        drawMenuButton();
    } else {
        drawMenuOverlay();
    }

    if (tapped) {
        handleTap(touchDetail.x, touchDetail.y);
    }

    delay(30);
}

