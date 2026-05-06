#include "LEDManager.h"

Adafruit_NeoPixel pixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
static LedState currentState = LED_BOOTING;
static unsigned long lastUpdate = 0;
static uint8_t brightness = 0;
static int8_t dir = 1;

void initLED() {
    pixel.begin();
    pixel.setBrightness(30);
    pixel.clear();
    pixel.show();
}

void setLedState(LedState state) {
    if (state != currentState) {
        currentState = state;
        lastUpdate = 0;
    }
}

void updateLEDTask() {
    unsigned long now = millis();
    switch (currentState) {
        case LED_BOOTING:
            if (now - lastUpdate > 200) {
                pixel.setPixelColor(0, ((now / 200) % 2) ? pixel.Color(100, 100, 100) : 0);
                pixel.show();
                lastUpdate = now;
            }
            break;
        case LED_WIFI_CONNECTING:
            if (now - lastUpdate > 10) {
                brightness += dir;
                if (brightness >= 100) dir = -1;
                if (brightness <= 0) dir = 1;
                pixel.setPixelColor(0, pixel.Color(0, 0, brightness));
                pixel.show();
                lastUpdate = now;
            }
            break;
        case LED_WIFI_OK:
            pixel.setPixelColor(0, pixel.Color(0, 20, 0));
            pixel.show();
            break;
        case LED_IDLE:
            if (now - lastUpdate > 15) {
                brightness += dir;
                if (brightness >= 50) dir = -1;
                if (brightness <= 5) dir = 1;
                pixel.setPixelColor(0, pixel.Color(0, brightness, 0));
                pixel.show();
                lastUpdate = now;
            }
            break;
        case LED_ADHAN:
            if (now - lastUpdate > 300) {
                pixel.setPixelColor(0, ((now / 300) % 2) ? pixel.Color(100, 100, 100) : 0);
                pixel.show();
                lastUpdate = now;
            }
            break;
        case LED_IQAMA:
            pixel.setPixelColor(0, pixel.Color(0, 0, 100));
            pixel.show();
            break;
        case LED_ALERT:
            if (now - lastUpdate > 500) {
                pixel.setPixelColor(0, ((now / 500) % 2) ? pixel.Color(100, 0, 0) : 0);
                pixel.show();
                lastUpdate = now;
            }
            break;
        case LED_ERROR:
            pixel.setPixelColor(0, pixel.Color(100, 0, 0));
            pixel.show();
            break;
    }
}