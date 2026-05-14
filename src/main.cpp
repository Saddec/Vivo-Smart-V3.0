#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <Preferences.h>

#include "AudioTask.h"
#include "SystemTask.h"

QueueHandle_t audioQueue;
SemaphoreHandle_t wifiMutex;
Preferences prefs;

// متغير عام لتخزين اسم الملف المُمرر إلى مهمة الصوت
char fileBuffer[128] = {0};

void setup() {
    Serial.begin(115200);
    Serial.println("بدء تشغيل Vivo Smart ESP32-S3");

    audioQueue = xQueueCreate(10, sizeof(AudioMessage));
    wifiMutex = xSemaphoreCreateMutex();

    // مهمة النظام (Core 0)
    xTaskCreatePinnedToCore(
        systemTask, "SystemTask", 16384, NULL, 4, NULL, 0
    );

    // مهمة الصوت (Core 1)
    xTaskCreatePinnedToCore(
        audioTask, "AudioTask", 16384, NULL, 6, NULL, 1
    );
}

void loop() {
    vTaskDelete(NULL);
}