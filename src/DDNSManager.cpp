// DDNSManager.cpp
#include "DDNSManager.h"
#include "SystemTask.h"
#include "AudioTask.h"
#include "EidMode.h"
#include "PrayerTimesEngine.h"
#include "EventLogger.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <esp_netif.h>
#include <time.h>

DDNSManager ddnsManager;

String DDNSManager::getGlobalIPv6() {
    if (WiFi.status() != WL_CONNECTED) return "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_ip6_addr_t ip6;
    if (netif && esp_netif_get_ip6_global(netif, &ip6) == ESP_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), IPV6STR, IPV62STR(ip6));
        String s(buf);
        if (!s.startsWith("fe80") && !s.startsWith("0000")) {
            return s;
        }
    }
    return "";
}

void DDNSManager::begin() {
    loadConfig();
    reloadTelegramConfig();
    _lastUpdate = 0; // trigger immediate update once WiFi is ready
    _lastTgPoll = 0;
}

void DDNSManager::loadConfig() {
    Preferences prefs;
    prefs.begin("ddns", true);
    _enabled = prefs.getBool("enabled", false);
    _provider = prefs.getString("provider", "noip");
    _domain = prefs.getString("domain", "");
    _user = prefs.getString("user", "");
    _pass = prefs.getString("pass", "");
    _useIPv6 = prefs.getBool("useIPv6", true);
    prefs.end();
    if (!_enabled) {
        _lastStatus = "غير مفعل";
    } else if (_lastStatus == "غير مفعل") {
        _lastStatus = "في انتظار التحديث...";
    }
}

void DDNSManager::reloadTelegramConfig() {
    Preferences prefs;
    prefs.begin("telegram", true);
    _tgEnabled = prefs.getBool("enabled", false);
    _tgToken = prefs.getString("token", "");
    _tgChatId = prefs.getString("chatId", "");
    prefs.end();
    if (!_tgEnabled) {
        _tgLastStatus = "غير مفعل";
    } else if (_tgToken.length() < 10) {
        _tgLastStatus = "يرجى إدخال Bot Token";
    } else {
        _tgLastStatus = "متصل وجاهز لاستقبال الأوامر";
    }
}

void DDNSManager::forceUpdate() {
    loadConfig();
    _lastUpdate = 0;
}

void DDNSManager::ddnsTaskEntry(void* param) {
    DDNSManager* self = static_cast<DDNSManager*>(param);
    self->performUpdate();
    self->_ddnsTaskRunning = false;
    vTaskDelete(NULL);
}

void DDNSManager::tgTaskEntry(void* param) {
    DDNSManager* self = static_cast<DDNSManager*>(param);
    self->pollTelegram();
    self->_tgTaskRunning = false;
    vTaskDelete(NULL);
}

void DDNSManager::loop() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Don't interrupt active audio playback with heavy TLS handshakes unless necessary
    bool audioIdle = (audioManager.getState() == AUDIO_IDLE);

    if (_enabled && !_ddnsTaskRunning) {
        if (_lastUpdate == 0 || (millis() - _lastUpdate >= UPDATE_INTERVAL)) {
            _lastUpdate = millis();
            _ddnsTaskRunning = true;
            xTaskCreatePinnedToCore(ddnsTaskEntry, "DdnsUpd", 6144, this, 1, NULL, 0);
        }
    }

    if (_tgEnabled && _tgToken.length() > 10 && !_tgTaskRunning && audioIdle) {
        if (_lastTgPoll == 0 || (millis() - _lastTgPoll >= TG_POLL_INTERVAL)) {
            _lastTgPoll = millis();
            _tgTaskRunning = true;
            xTaskCreatePinnedToCore(tgTaskEntry, "TgPoll", 8192, this, 1, NULL, 0);
        }
    }
}

void DDNSManager::performUpdate() {
    if (_domain.length() == 0) {
        _lastStatus = "النطاق (Domain) فارغ";
        return;
    }

    String ipv6 = _useIPv6 ? getGlobalIPv6() : "";
    String url = "";
    String authEncoded = "";

    if (_provider == "duckdns") {
        // DuckDNS uses domain (without .duckdns.org or with it) and token (stored in _pass or _user)
        String sub = _domain;
        sub.replace(".duckdns.org", "");
        String token = _pass.length() > 0 ? _pass : _user;
        if (token.length() == 0) {
            _lastStatus = "يرجى إدخال DuckDNS Token في خانة كلمة المرور أو المستخدم";
            return;
        }
        url = "https://www.duckdns.org/update?domains=" + sub + "&token=" + token;
        if (ipv6.length() > 0) {
            url += "&ipv6=" + ipv6;
        }
    } else if (_provider == "dynu") {
        if (_user.length() == 0) {
            _lastStatus = "اسم المستخدم فارغ";
            return;
        }
        String auth = _user + ":" + _pass;
        authEncoded = base64::encode(auth);
        authEncoded.replace("\r", "");
        authEncoded.replace("\n", "");
        url = "https://api.dynu.com/nic/update?hostname=" + _domain;
        if (ipv6.length() > 0) {
            url += "&myipv6=" + ipv6;
        }
    } else {
        // Default: No-IP
        if (_user.length() == 0) {
            _lastStatus = "اسم المستخدم فارغ";
            return;
        }
        String auth = _user + ":" + _pass;
        authEncoded = base64::encode(auth);
        authEncoded.replace("\r", "");
        authEncoded.replace("\n", "");
        url = "https://dynupdate.no-ip.com/nic/update?hostname=" + _domain;
        if (ipv6.length() > 0) {
            url += "&myipv6=" + ipv6;
        }
    }

    LOG_WF("WIFI", "DDNS (%s) updating %s (IPv6: %s)", _provider.c_str(), _domain.c_str(), ipv6.length() ? ipv6.c_str() : "auto-IPv4");

    int httpCode = -1;
    String payload = "";

    {
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        HTTPClient http;
        http.setTimeout(5000);
        if (http.begin(secureClient, url)) {
            if (authEncoded.length() > 0) {
                http.addHeader("Authorization", "Basic " + authEncoded);
            }
            http.addHeader("User-Agent", "VivoSmart/3.0 admin@vivosmart.local");
            httpCode = http.GET();
            if (httpCode > 0) {
                payload = http.getString();
                payload.trim();
            }
            http.end();
        }
    }

    // Fallback to HTTP for No-IP if HTTPS fails
    if (httpCode <= 0 && _provider == "noip") {
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(4000);
        String httpUrl = url;
        httpUrl.replace("https://", "http://");
        if (http.begin(client, httpUrl)) {
            if (authEncoded.length() > 0) {
                http.addHeader("Authorization", "Basic " + authEncoded);
            }
            http.addHeader("User-Agent", "VivoSmart/3.0 admin@vivosmart.local");
            httpCode = http.GET();
            if (httpCode > 0) {
                payload = http.getString();
                payload.trim();
            }
            http.end();
        }
    }

    time_t now = time(nullptr);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tinfo.tm_hour, tinfo.tm_min, tinfo.tm_sec);
    _lastUpdateTime = String(timeBuf);
    _lastResponse = payload;

    if (httpCode == 200) {
        if (payload.startsWith("good") || payload.startsWith("nochg") || payload == "OK") {
            _lastStatus = "✅ متصل ومحدث بنجاح (" + payload + ")";
            _lastUpdatedIp = ipv6.length() > 0 ? ipv6 : WiFi.localIP().toString();
            LOG_WF("WIFI", "DDNS update success: %s", payload.c_str());
        } else if (payload.indexOf("badauth") >= 0 || payload == "KO") {
            _lastStatus = "❌ خطأ في اسم المستخدم أو كلمة المرور/التوكن (" + payload + ")";
            LOG_E("WIFI", "DDNS authentication failed: %s", payload.c_str());
        } else if (payload.indexOf("nohost") >= 0) {
            _lastStatus = "❌ النطاق غير موجود في حسابك (nohost)";
            LOG_E("WIFI", "DDNS host not found: %s", payload.c_str());
        } else {
            _lastStatus = "⚠️ رد الخادم: " + payload;
        }
    } else {
        _lastStatus = "❌ فشل الاتصال بخادم DDNS (كود " + String(httpCode) + ")";
        LOG_W("WIFI", "DDNS update failed with HTTP code %d", httpCode);
    }
}

String DDNSManager::getStatusJson() {
    DynamicJsonDocument doc(768);
    Preferences prefs;
    prefs.begin("ddns", true);
    doc["enabled"] = prefs.getBool("enabled", false);
    doc["provider"] = prefs.getString("provider", "noip");
    doc["domain"] = prefs.getString("domain", "");
    doc["user"] = prefs.getString("user", "");
    doc["useIPv6"] = prefs.getBool("useIPv6", true);
    doc["hasPass"] = prefs.getString("pass", "").length() > 0;
    prefs.end();

    String ipv6 = getGlobalIPv6();
    doc["status"] = _lastStatus;
    doc["response"] = _lastResponse;
    doc["lastUpdateTime"] = _lastUpdateTime;
    doc["globalIPv6"] = ipv6;
    doc["localIPv4"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
    String json;
    serializeJson(doc, json);
    return json;
}

String DDNSManager::getTelegramStatusJson() {
    DynamicJsonDocument doc(512);
    Preferences prefs;
    prefs.begin("telegram", true);
    doc["enabled"] = prefs.getBool("enabled", false);
    doc["hasToken"] = prefs.getString("token", "").length() > 10;
    doc["chatId"] = prefs.getString("chatId", "");
    prefs.end();
    doc["status"] = _tgLastStatus;
    String json;
    serializeJson(doc, json);
    return json;
}

static String urlEncodeTg(const String& str) {
    String encoded = "";
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < str.length(); i++) {
        uint8_t c = (uint8_t)str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

bool DDNSManager::sendTelegramMessage(const String& chatId, const String& text) {
    if (_tgToken.length() < 10 || chatId.length() == 0) return false;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4500);
    String url = "https://api.telegram.org/bot" + _tgToken + "/sendMessage";
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = "chat_id=" + chatId + "&text=" + urlEncodeTg(text);
    int code = http.POST(body);
    http.end();
    return (code == 200);
}

void DDNSManager::handleTelegramCommand(const String& chatId, const String& text, const String& fromName) {
    // Security check: if user configured an authorized Chat ID, reject others
    if (_tgChatId.length() > 0 && _tgChatId != chatId) {
        sendTelegramMessage(chatId, "⛔ غير مصرح لهذا الحساب بالتحكم في الجهاز.\nمعرف المحادثة الخاص بك (Chat ID): " + chatId);
        return;
    }

    String cmd = text;
    cmd.trim();

    extern PrayerTimesResult getTodayPrayerSafe();
    PrayerTimesResult pt = getTodayPrayerSafe();
    String ipv6 = getGlobalIPv6();
    String ipv4 = WiFi.localIP().toString();

    if (cmd == "/start" || cmd == "/help" || cmd == "مساعدة") {
        String msg = "🕌 أهلاً بك في نظام التحكم عن بعد Vivo Smart V3.0\n";
        msg += "معرف المحادثة (Chat ID): " + chatId + "\n\n";
        msg += "📋 الأوامر المتاحة من أي مكان في العالم:\n";
        msg += "• /status - عرض حالة الجهاز، الوقت، الصوت، والروابط\n";
        msg += "• /prayers - عرض مواقيت الصلاة اليوم\n";
        msg += "• /stop - إيقاف الصوت الحالي فوراً\n";
        msg += "• /vol 25 - تغيير مستوى الصوت (0 إلى 30)\n";
        msg += "• /eid_on - تفعيل وضع العيد يدوياً\n";
        msg += "• /eid_off - إيقاف وضع العيد اليدوي\n";
        msg += "• /takbeer - تشغيل تكبيرات العيد الآن\n";
        msg += "• /reboot - إعادة تشغيل الجهاز\n";
        sendTelegramMessage(chatId, msg);
    } else if (cmd == "/status" || cmd == "الحالة") {
        time_t now = time(nullptr);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02d %02d:%02d", tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday, tinfo.tm_hour, tinfo.tm_min);

        String msg = "📊 حالة جهاز Vivo Smart V3.0:\n";
        msg += "🕒 وقت الجهاز: " + String(tbuf) + "\n";
        msg += "📶 الشبكة: " + WiFi.SSID() + " (" + ipv4 + ")\n";
        if (ipv6.length() > 0) {
            msg += "🌐 رابط IPv6 العالمي المباشر:\nhttp://[" + ipv6 + "]/\n";
        }
        if (_enabled && _domain.length() > 0) {
            msg += "🔗 رابط DDNS:\nhttp://" + _domain + "/\n";
        }
        msg += "🔊 المشغل: " + audioManager.getAudioDescription() + " (الصوت: " + String(audioManager.getVolume()) + "/30)\n";
        msg += "🌙 وضع العيد: " + String(isEidMode() ? "مفعل 🟢" : "متوقف ⚪");
        sendTelegramMessage(chatId, msg);
    } else if (cmd == "/prayers" || cmd == "المواقيت") {
        if (!pt.valid) {
            sendTelegramMessage(chatId, "⚠️ مواقيت الصلاة غير جاهزة حالياً.");
        } else {
            String msg = "🕌 مواقيت الصلاة اليوم:\n";
            msg += "• الفجر: " + pt.fajr + "\n";
            msg += "• الشروق: " + pt.sunrise + "\n";
            msg += "• الظهر: " + pt.dhuhr + "\n";
            msg += "• العصر: " + pt.asr + "\n";
            msg += "• المغرب: " + pt.maghrib + "\n";
            msg += "• العشاء: " + pt.isha;
            sendTelegramMessage(chatId, msg);
        }
    } else if (cmd == "/stop" || cmd == "ايقاف" || cmd == "إيقاف") {
        AudioMessage msgCmd = {CMD_STOP, 0, 0, 0, 0, 0, 0};
        xQueueSend(audioQueue, &msgCmd, 0);
        sendTelegramMessage(chatId, "⏹ تم إيقاف تشغيل الصوت.");
    } else if (cmd.startsWith("/vol ")) {
        int v = cmd.substring(5).toInt();
        if (v < 0) v = 0;
        if (v > 30) v = 30;
        AudioMessage msgCmd = {CMD_SET_VOLUME, 0, 0, 0, (uint8_t)v, 0, 0};
        xQueueSend(audioQueue, &msgCmd, 0);
        sendTelegramMessage(chatId, "🔊 تم ضبط مستوى الصوت على: " + String(v) + "/30");
    } else if (cmd == "/eid_on") {
        setEidMode(true);
        sendTelegramMessage(chatId, "🟢 تم تفعيل وضع العيد اليدوي.");
    } else if (cmd == "/eid_off") {
        setEidMode(false);
        sendTelegramMessage(chatId, "⚪ تم إيقاف وضع العيد اليدوي.");
    } else if (cmd == "/takbeer" || cmd == "تكبير") {
        String tf = getResolvedEidTakbeerFile();
        if (tf.length() > 0) {
            audioManager.setAudioDescription("تكبيرات العيد (عن بعد)");
            sendPlayCommand(tf.c_str(), 1, 0, 27);
            sendTelegramMessage(chatId, "🕋 جاري تشغيل تكبيرات العيد: " + tf);
        } else {
            sendTelegramMessage(chatId, "❌ لم يتم العثور على ملف تكبيرات العيد على بطاقة SD.");
        }
    } else if (cmd == "/reboot") {
        sendTelegramMessage(chatId, "🔄 جاري إعادة تشغيل الجهاز...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP.restart();
    } else {
        sendTelegramMessage(chatId, "❓ أمر غير معروف. أرسل /help لعرض قائمة الأوامر.");
    }
}

void DDNSManager::pollTelegram() {
    if (!_tgEnabled || _tgToken.length() < 10) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4500);
    String url = "https://api.telegram.org/bot" + _tgToken + "/getUpdates?timeout=0&limit=3";
    if (_tgLastUpdateId > 0) {
        url += "&offset=" + String(_tgLastUpdateId + 1);
    }

    if (!http.begin(client, url)) {
        _tgLastStatus = "⚠️ تعذر الاتصال بخادم تيليجرام";
        return;
    }

    int code = http.GET();
    if (code == 200) {
        String payload = http.getString();
        http.end();
        DynamicJsonDocument doc(4096);
        if (!deserializeJson(doc, payload) && doc["ok"] == true) {
            _tgLastStatus = "✅ متصل ببوت تيليجرام وجاهز";
            JsonArray res = doc["result"].as<JsonArray>();
            for (JsonObject item : res) {
                int64_t updateId = item["update_id"].as<int64_t>();
                if (updateId > _tgLastUpdateId) {
                    _tgLastUpdateId = updateId;
                }
                JsonObject message = item["message"];
                if (!message.isNull()) {
                    String chatId = String(message["chat"]["id"].as<int64_t>());
                    String text = message["text"] | "";
                    String fromName = message["from"]["first_name"] | "";
                    // Save first seen chatId automatically if user hasn't locked a chatId yet
                    if (_tgChatId.length() == 0 && chatId.length() > 0) {
                        _tgChatId = chatId;
                        Preferences prefs;
                        prefs.begin("telegram", false);
                        prefs.putString("chatId", _tgChatId);
                        prefs.end();
                    }
                    if (text.length() > 0) {
                        LOG_SYS("SYSTEM", "Telegram command from %s (%s): %s", fromName.c_str(), chatId.c_str(), text.c_str());
                        handleTelegramCommand(chatId, text, fromName);
                    }
                }
            }
        }
    } else {
        http.end();
        if (code == 401 || code == 404) {
            _tgLastStatus = "❌ Bot Token غير صحيح (كود " + String(code) + ")";
        }
    }
}
