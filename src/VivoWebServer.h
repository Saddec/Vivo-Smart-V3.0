#ifndef VIVOWEBSERVER_H
#define VIVOWEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

void startWebServer();
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

extern AsyncWebServer server;
extern Preferences prefs;
extern QueueHandle_t audioQueue;

#endif