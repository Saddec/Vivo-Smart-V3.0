#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN    48    // WS2812 on ESP32-S3 DevKitC (adjust if necessary)
#define NUM_LEDS   1

enum LedState {
    LED_BOOTING,
    LED_WIFI_CONNECTING,
    LED_WIFI_OK,
    LED_IDLE,
    LED_ADHAN,
    LED_IQAMA,
    LED_ALERT,
    LED_ERROR
};

void initLED();
void setLedState(LedState state);
void updateLEDTask();

#endif