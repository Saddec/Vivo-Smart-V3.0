// DDNSManager.h
#ifndef DDNSMANAGER_H
#define DDNSMANAGER_H

#include <Arduino.h>

class DDNSManager {
public:
    void begin();
    void loop();
    void forceUpdate();
private:
    bool _enabled = false;
    String _domain;
    String _user;
    String _pass;
    uint32_t _lastUpdate = 0;
    const uint32_t UPDATE_INTERVAL = 300000; // 5 minutes
    
    void loadConfig();
    void performUpdate();
};

extern DDNSManager ddnsManager;

#endif
