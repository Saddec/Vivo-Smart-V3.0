#include "AudioTask.h"
#include <SD.h>
#include <SPI.h>
#include "Audio.h"

#define I2S_BCLK   26
#define I2S_LRCK   25
#define I2S_DOUT   22
#define SD_CS      5

AudioManager audioManager;

void AudioManager::begin() {
    SPI.begin(18, 19, 23);
    if (!SD.begin(SD_CS)) {
        Serial.println("[Audio] فشل تهيئة بطاقة SD!");
    } else {
        Serial.println("[Audio] SD Card OK.");
    }
    _audio = new Audio();
    _audio->setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
    _audio->setVolume(15);
    _state = AUDIO_IDLE;
    _currentPriority = 0;
    _currentFile = "";
    _playStartTime = 0;
    _customDuration = 0;
}

bool AudioManager::playFile(const char* path, int priority, uint32_t duration) {
    if (!_audio) return false;
    if (_state == AUDIO_PLAYING && priority < _currentPriority) return false;
    if (_state != AUDIO_IDLE) _audio->stopSong();
    String fullPath = "/" + String(path);
    if (!_audio->connecttoSD(fullPath.c_str())) {
        _state = AUDIO_IDLE;
        return false;
    }
    _state = AUDIO_PLAYING;
    _currentPriority = priority;
    _currentFile = path;
    _playStartTime = millis();
    _customDuration = duration;
    if (priority == 3) _audio->setVolume(25);
    else if (priority == 2) _audio->setVolume(22);
    else _audio->setVolume(15);
    return true;
}

void AudioManager::setVolume(uint8_t vol) { if (_audio) _audio->setVolume(vol); }
void AudioManager::stop() { if (_audio) { _audio->stopSong(); _state = AUDIO_IDLE; _currentPriority = 0; _customDuration = 0; } }
void AudioManager::pause() { if (_audio && _state == AUDIO_PLAYING) { _audio->pauseResume(); _state = AUDIO_PAUSED; } }
void AudioManager::resume() { if (_audio && _state == AUDIO_PAUSED) { _audio->pauseResume(); _state = AUDIO_PLAYING; } }
AudioState AudioManager::getState() {
    if (_state == AUDIO_PLAYING && !_audio->isRunning()) { _state = AUDIO_IDLE; _currentPriority = 0; }
    if (_state == AUDIO_PLAYING && _customDuration > 0 && (millis() - _playStartTime >= _customDuration * 1000)) stop();
    return _state;
}
const char* AudioManager::getCurrentFile() { return _currentFile.c_str(); }
void AudioManager::audioOnStop(void *userData) {
    AudioManager* self = static_cast<AudioManager*>(userData);
    if (self) { self->_state = AUDIO_IDLE; self->_currentPriority = 0; self->_currentFile = ""; self->_customDuration = 0; }
}

void audioTask(void *pvParameters) {
    audioManager.begin();
    AudioMessage msg;
    for (;;) {
        if (xQueueReceive(audioQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.cmd) {
                case CMD_PLAY_FILE: {
                    const char* path;
                    if (msg.param1 == 0) path = fileBuffer;
                    else { static char buf[64]; snprintf(buf, sizeof(buf), "/audio/%04d.mp3", msg.param1); path = buf; }
                    audioManager.playFile(path, msg.priority, msg.param2);
                    break;
                }
                case CMD_STOP: audioManager.stop(); break;
                case CMD_SET_VOLUME: audioManager.setVolume(msg.param1); break;
                case CMD_PAUSE: audioManager.pause(); break;
                case CMD_RESUME: audioManager.resume(); break;
            }
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}