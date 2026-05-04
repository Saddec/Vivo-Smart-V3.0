#include "GPIOManager.h"
#include "SystemTask.h"   // <--- الحل
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

struct InputMapping { uint8_t pin; String file; bool lastState; };
struct OutputMapping { uint8_t pin; String alertFile; int durationSec; };
static std::vector<InputMapping> inputMappings;
static std::vector<OutputMapping> outputMappings;
static std::vector<std::pair<uint8_t, unsigned long>> outputTimers;

// تعريف خارجي للدوال
void saveGPIOMappings() {
    DynamicJsonDocument doc(2048); JsonObject root=doc.to<JsonObject>();
    JsonArray inputs=root.createNestedArray("inputs"); for(auto& in:inputMappings){ JsonObject o=inputs.createNestedObject(); o["pin"]=in.pin; o["file"]=in.file; }
    JsonArray outputs=root.createNestedArray("outputs"); for(auto& out:outputMappings){ JsonObject o=outputs.createNestedObject(); o["pin"]=out.pin; o["alert"]=out.alertFile; o["duration"]=out.durationSec; }
    String json; serializeJson(doc,json);
    Preferences prefs; prefs.begin("gpio",false); prefs.putString("mappings",json); prefs.end();
}
void loadGPIOMappings() {
    Preferences prefs; prefs.begin("gpio",true); String json=prefs.getString("mappings","{}"); prefs.end();
    DynamicJsonDocument doc(2048);
    if(!deserializeJson(doc,json)){
        JsonObject root=doc.as<JsonObject>();
        if(root.containsKey("inputs")){ JsonArray inArr=root["inputs"].as<JsonArray>(); for(JsonObject o:inArr){ InputMapping m; m.pin=o["pin"]; m.file=o["file"].as<String>(); m.lastState=HIGH; inputMappings.push_back(m); } }
        if(root.containsKey("outputs")){ JsonArray outArr=root["outputs"].as<JsonArray>(); for(JsonObject o:outArr){ OutputMapping m; m.pin=o["pin"]; m.alertFile=o["alert"].as<String>(); m.durationSec=o["duration"]; outputMappings.push_back(m); } }
    }
}

void initGPIO() {
    loadGPIOMappings();
    for(auto& in:inputMappings){ pinMode(in.pin,INPUT_PULLUP); in.lastState=digitalRead(in.pin); }
    for(auto& out:outputMappings){ pinMode(out.pin,OUTPUT); digitalWrite(out.pin,LOW); }
}
void checkGPIOInputs() {
    for(auto& in:inputMappings){ bool cur=digitalRead(in.pin); if(in.lastState==HIGH && cur==LOW){ sendPlayCommand(in.file.c_str(),1,0); } in.lastState=cur; }
}
void addInputMapping(int pin, const String& file) {
    inputMappings.erase(std::remove_if(inputMappings.begin(),inputMappings.end(),[pin](const InputMapping& m){return m.pin==pin;}),inputMappings.end());
    InputMapping m; m.pin=pin; m.file=file; m.lastState=HIGH; pinMode(pin,INPUT_PULLUP); inputMappings.push_back(m); saveGPIOMappings();
}
void addOutputMapping(int pin, const String& alertFile, int durationSec) {
    outputMappings.erase(std::remove_if(outputMappings.begin(),outputMappings.end(),[pin](const OutputMapping& m){return m.pin==pin;}),outputMappings.end());
    OutputMapping m; m.pin=pin; m.alertFile=alertFile; m.durationSec=durationSec; pinMode(pin,OUTPUT); digitalWrite(pin,LOW); outputMappings.push_back(m); saveGPIOMappings();
}
String getGpioMappingsJson() {
    DynamicJsonDocument doc(2048); JsonObject root=doc.to<JsonObject>();
    JsonArray inputs=root.createNestedArray("inputs"); for(auto& in:inputMappings){ JsonObject o=inputs.createNestedObject(); o["pin"]=in.pin; o["file"]=in.file; }
    JsonArray outputs=root.createNestedArray("outputs"); for(auto& out:outputMappings){ JsonObject o=outputs.createNestedObject(); o["pin"]=out.pin; o["alert"]=out.alertFile; o["duration"]=out.durationSec; }
    String json; serializeJson(doc,json); return json;
}
void setOutputForAlert(const String& alertName, int durationSec) {
    for(auto& out:outputMappings){ if(alertName.indexOf(out.alertFile)>=0){ digitalWrite(out.pin,HIGH); outputTimers.push_back({out.pin,millis()+out.durationSec*1000UL}); } }
}
void checkOutputTimers() {
    for(auto it=outputTimers.begin();it!=outputTimers.end();){ if(millis()>=it->second){ digitalWrite(it->first,LOW); it=outputTimers.erase(it); } else ++it; }
}