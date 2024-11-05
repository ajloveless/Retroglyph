#include "esp_pm.h"
#include "CompositeGraphics.h"
#include "CompositeColorOutput.h"
#include "font8x8.h"

// Pin definitions
#define AUDIOPORT 27
#define POT1 36
#define POT2 39
#define POT3 34
#define POT4 35
#define POT5 32
#define POT6 33

#define USE_ATARI_COLORS
#define XRES CompositeColorOutput::XRES
#define YRES CompositeColorOutput::YRES
#define CHW 8
#define CHH 8

CompositeGraphics graphics(CompositeColorOutput::XRES, CompositeColorOutput::YRES, 1337);
CompositeColorOutput composite(CompositeColorOutput::NTSC);

Font<CompositeGraphics> font(CHW, CHH, font8x8::pixels);

// Visual parameters
const int rows = YRES / CHH;
const int cols = XRES / CHW;
const int resolution = cols;
int buffer[resolution];
int segmentSize = XRES / resolution;

// Color palette
unsigned char palette[4][2];
int colorCenter = random(3, 28);
int spread = 0;

// Text pattern 
const char* text = "!@#$%^&*";
const char* string = text;
int pattern[6] = {0, strlen(text), 1, 0, strlen(text), 1};

// BPM
int threshold = 150;
int peakHoldTime = 150;
unsigned long lastPeakTime = 0;
const int bpmSampleCount = 5;
int bpmIntervals[bpmSampleCount];
int bpmIndex = 0;
unsigned long lastBeatTime = 0;
float bpm = 0;
const int bpmAverageCount = 4;
float bpmHistory[bpmAverageCount] = {0};
int bpmHistoryIndex = 0;
float smoothedBPM = 0;

// Audio activity
bool audioActive = false;
unsigned long audioInactiveStartTime = 0;
unsigned long audioActiveStartTime = 0;

// Color palette update
void color() {
    for (int i = 0; i < sizeof(palette) / sizeof(*palette); i++) {
        palette[i][0] = 8 * (colorCenter) + (i * spread);
        palette[i][1] = palette[i][0] + (i * spread);
        if (abs(palette[i][0] - palette[i][1]) < 3) {
            palette[i][1] += 7;
        }
    }
}

void setup() {
    Serial.begin(115200);

    esp_pm_lock_handle_t powerManagementLock;
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "compositeCorePerformanceLock", &powerManagementLock);
    esp_pm_lock_acquire(powerManagementLock);

    spread = map(analogRead(POT4), 0, 4095, 0, 64);
    colorCenter = map(analogRead(POT6), 0, 4095, 3, 28);
    color();

    esp_random();
    composite.init();
    graphics.init();
    graphics.setFont(font);
}

// Render text 
void textRender(const char* str, int start, int length) {
    const char* substr = str + start;
    int index = 0;
    for (int i = 0; i < length; i++) {
        graphics.setTextColor(palette[index % 255][0], palette[index % 255][1]);
        graphics.print(&substr[index]);
        index = (index + 1) % length;
    }
}

// Draw pattern to screen
void draw(const char* str) {
    graphics.setHue(0);
    graphics.begin(0);

    graphics.setCursor(0, 0);
    int iterations = rows * cols;

    for (int i = 0; i < iterations; i += pattern[1]) {
        textRender(text, pattern[0], pattern[1]);
    }

    graphics.end();
}

// Adjust threshold dynamically
void adjustThreshold(int baseline) {
    static int dynamicThreshold = threshold;
    if (millis() - lastPeakTime < 500) {
        dynamicThreshold += 1;
    } else {
        dynamicThreshold -= 1;
    }
    threshold = constrain(dynamicThreshold, 5, 100);
}

// Detect beat and calculate BPM
void detectBeat(int avg) {
    unsigned long currentBeatTime = millis();
    if (lastBeatTime > 0) {
        int interval = currentBeatTime - lastBeatTime;

        if (interval < 300) return;

        bpmIntervals[bpmIndex++] = interval;
        if (bpmIndex >= bpmSampleCount) bpmIndex = 0;

        float avgInterval = 0;
        for (int i = 0; i < bpmSampleCount; i++) {
            avgInterval += bpmIntervals[i];
        }
        avgInterval /= bpmSampleCount;

        bpm = 60000.0 / avgInterval;
        if (bpm >= 40 && bpm <= 200) {
            addBPMToHistory(bpm);
            smoothedBPM = calculateSmoothedBPM();
            Serial.printf("Detected Smoothed BPM: %.2f, Audio Level: %d\n", smoothedBPM, avg);
            triggerBPMEffect(smoothedBPM);
        }
    }
    lastBeatTime = currentBeatTime;
}

void addBPMToHistory(float newBPM) {
    bpmHistory[bpmHistoryIndex++] = newBPM;
    if (bpmHistoryIndex >= bpmAverageCount) bpmHistoryIndex = 0;
}

float calculateSmoothedBPM() {
    float sum = 0;
    for (int i = 0; i < bpmAverageCount; i++) {
        sum += bpmHistory[i];
    }
    return sum / bpmAverageCount;
}

// Changes to happen on beat
void triggerBPMEffect(float bpm) {
    pattern[1] = random(1, strlen(string));
    pattern[0] = random(1, strlen(string));
    pattern[2] = random(1, 4);
    pattern[4] = random(1, strlen(string));
    spread = map(bpm, 60, 180, 10, 64);
    colorCenter = map(bpm, 60, 180, 3, 28);
    color();
}

void loop() {
    unsigned int sum = 0;
    for (int i = 0; i < resolution; i++) {
        buffer[i] = analogRead(AUDIOPORT);
        sum += buffer[i];
    }
    int avg = sum / resolution;

    if (audioActive) {
        for (int i = 0; i < resolution; i++) {
            int signal = buffer[i];
            if (signal > avg + threshold) {
                if (millis() - lastPeakTime > peakHoldTime) {
                    lastPeakTime = millis();
                    detectBeat(avg);
                }
            }
        }

        adjustThreshold(avg);

        if (avg < 5) {
            if (audioInactiveStartTime == 0) {
                audioInactiveStartTime = millis();
            } else if (millis() - audioInactiveStartTime >= 2000) {
                audioActive = false;
            }
        } else {
            audioInactiveStartTime = 0;
        }
    } else {
        if (avg > 5) {
            if (audioActiveStartTime == 0) {
                audioActiveStartTime = millis();
            } else if (millis() - audioActiveStartTime >= 32) {
                audioActive = true;
                audioActiveStartTime = 0;
            }
        } else {
            audioActiveStartTime = 0;
        }
    }

    int prevValues[6] = {0, 1, 1, 0, 10, 3};
    int *targets[6] = {&pattern[0], &pattern[1], &pattern[2], &pattern[4], &spread, &colorCenter};
    const int targetRanges[6][2] = {{0, strlen(string)}, {1, strlen(string)}, {1, 4}, {0, strlen(string)}, {10, 64}, {3, 28}};
    const int pots[6] = {POT1, POT2, POT3, POT5, POT4, POT6};
    const int changeThreshold = 2;

    // Manual control if audio is inactive
    if (!audioActive) {
        for (int i = 0; i < 6; i++) {
            int newValue = map(analogRead(pots[i]), 0, 4095, targetRanges[i][0], targetRanges[i][1]);
            if (abs(newValue - prevValues[i]) > changeThreshold) {
                *targets[i] = newValue;
                prevValues[i] = newValue;
            }
        }
        color();
    }

    // Send text to the display
    draw(string);
    composite.sendFrameHalfResolution(&graphics.frame);
}