#ifndef GPIOMANAGER_H
#define GPIOMANAGER_H

#include <Arduino.h>
#include <vector>

void initGPIO();
void checkGPIOInputs();
void checkOutputTimers();
void addInputMapping(int pin, const String& file);
void addOutputMapping(int pin, const String& alertFile, int durationSec);
String getGpioMappingsJson();
void setOutputForAlert(const String& alertName, int durationSec);

void loadGPIOMappings();
void saveGPIOMappings(); 

#endif