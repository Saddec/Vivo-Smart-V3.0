// DDNSManager.h
#ifndef DDNSMANAGER_H
#define DDNSMANAGER_H

#include <Arduino.h>

class DDNSManager {
public:
    void begin();
    void loop();
    void forceUpdate();
    String getStatusJson();
    static String getGlobalIPv6();

    // Telegram Remote Control Config
    void reloadTelegramConfig();
    String getTelegramStatusJson();

private:
    bool _enabled = false;
    String _provider = "noip"; // "noip", "duckdns", "dynu"
    String _domain;
    String _user;
    String _pass;
    bool _useIPv6 = true;
    uint32_t _lastUpdate = 0;
    const uint32_t UPDATE_INTERVAL = 300000; // 5 minutes
    String _lastStatus = "غير مفعل";
    String _lastResponse = "";
    String _lastUpdatedIp = "";
    String _lastUpdateTime = "";

    // Telegram Bot State
    bool _tgEnabled = false;
    String _tgToken = "";
    String _tgChatId = "";
    int64_t _tgLastUpdateId = 0;
    uint32_t _lastTgPoll = 0;
    const uint32_t TG_POLL_INTERVAL = 6000; // 6 seconds
    String _tgLastStatus = "غير مفعل";
    bool _tgTaskRunning = false;

    void loadConfig();
    void performUpdate();
    void pollTelegram();
    void handleTelegramCommand(const String& chatId, const String& text, const String& fromName);
    bool sendTelegramMessage(const String& chatId, const String& text);
    static void ddnsTaskEntry(void* param);
    static void tgTaskEntry(void* param);
    bool _ddnsTaskRunning = false;
};

extern DDNSManager ddnsManager;

#endif
