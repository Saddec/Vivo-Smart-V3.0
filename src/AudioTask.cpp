#include "AudioTask.h"
#include "SDManager.h"
#include <SD.h>
#include "Audio.h"

#define I2S_BCLK 16
#define I2S_LRCK 17
#define I2S_DOUT 18

static String normalizeAudioPath(const char* path) {
    String fullPath = String(path);
    fullPath.trim();
    if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
    fullPath.replace("//", "/");
    return fullPath;
}

AudioManager audioManager;

void AudioManager::begin() {
    _audioMutex = xSemaphoreCreateRecursiveMutex();
    if (!initSDCard(true)) {
        Serial.printf("[Audio] SD Card failed: %s\n", getLastSDError().c_str());
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
    _loopDuration = 0;
    _loopEndTime = 0;
}

bool AudioManager::playFile(const char* path, int priority, uint32_t duration, uint8_t volume, uint32_t loopDuration, int repeatCount) {
    if (!_audio) return false;
    if (!isSDReady()) {
        Serial.printf("[Audio] SD not ready: %s\n", getLastSDError().c_str());
        return false;
    }
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return false;
    if (_adhanPlaying && priority < 2) { xSemaphoreGiveRecursive(_audioMutex); return false; }
    if (_state == AUDIO_PLAYING && priority < _currentPriority) { xSemaphoreGiveRecursive(_audioMutex); return false; }

    if (_respectAdhan && !_playlist.empty() && priority == 3 && _state == AUDIO_PLAYING) {
        _playlistSuspended = true;
        _suspendTime = millis();
        stop();
    }

    if (_state != AUDIO_IDLE) _audio->stopSong();

    _adhanPlaying = (priority >= 2);

    String fullPath = normalizeAudioPath(path);
    if (!SD.exists(fullPath)) {
        Serial.printf("[Audio] Missing file: %s\n", fullPath.c_str());
        _state = AUDIO_IDLE;
        xSemaphoreGiveRecursive(_audioMutex);
        return false;
    }
    if (!_audio->connecttoSD(fullPath.c_str())) {
        Serial.printf("[Audio] Cannot play: %s\n", fullPath.c_str());
        _state = AUDIO_IDLE;
        xSemaphoreGiveRecursive(_audioMutex);
        return false;
    }
    _state = AUDIO_PLAYING;
    _currentPriority = priority;
    _currentFile = fullPath;
    _playStartTime = millis();
    _customDuration = duration;
    _loopDuration = loopDuration;
    _repeatCount = repeatCount;
    if (loopDuration > 0) {
        _loopEndTime = millis() + (loopDuration * 1000);
        _lastPlayedFile = fullPath;
        _lastPriority = priority;
        _lastVolume = volume;
    } else {
        _loopEndTime = 0;
        _lastPlayedFile = "";
    }

    if (volume > 0) _audio->setVolume(volume);
    else if (priority == 3) _audio->setVolume(25);
    else if (priority == 2) _audio->setVolume(22);
    else _audio->setVolume(15);
    xSemaphoreGiveRecursive(_audioMutex);
    return true;
}

bool AudioManager::playPlaylist(const String& list, uint8_t volume, bool respectAdhan, int pauseAfterAdhan) {
    std::vector<String> tempList;
    int idx = 0, last = 0;
    while (idx < list.length()) {
        if (list[idx] == ',') {
            if (idx > last) tempList.push_back(list.substring(last, idx));
            last = idx + 1;
        }
        idx++;
    }
    if (idx > last) tempList.push_back(list.substring(last));
    if (tempList.empty()) return false;

    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return false;
    _playlist = std::move(tempList);
    _playlistIndex = 0;
    _playlistVolume = volume;
    _respectAdhan = respectAdhan;
    _pauseAfterAdhan = pauseAfterAdhan;
    _playlistSuspended = false;
    xSemaphoreGiveRecursive(_audioMutex);

    return playFile(_playlist[0].c_str(), 0, 0, volume);
}

void AudioManager::advancePlaylist() {
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_playlistIndex + 1 < _playlist.size()) {
        _playlistIndex++;
        String nextFile = _playlist[_playlistIndex];
        xSemaphoreGiveRecursive(_audioMutex);
        playFile(nextFile.c_str(), 0, 0, _playlistVolume);
    } else {
        _playlist.clear();
        _state = AUDIO_IDLE;
        xSemaphoreGiveRecursive(_audioMutex);
    }
}

void AudioManager::checkPlaylistResume() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (_playlistSuspended && _respectAdhan && _playlist.size() > 0) {
        if (millis() - _suspendTime >= (unsigned long)(_pauseAfterAdhan * 1000)) {
            _playlistSuspended = false;
            if (_playlistIndex < _playlist.size()) {
                String resumeFile = _playlist[_playlistIndex];
                xSemaphoreGiveRecursive(_audioMutex);
                playFile(resumeFile.c_str(), 0, 0, _playlistVolume);
                return;
            }
        }
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::setVolume(uint8_t vol) {
    if (_adhanPlaying) {
        Serial.println("[Audio] Adhan/Iqama playing, volume change blocked");
        return;
    }
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio) _audio->setVolume(vol);
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::stop() {
    if (_adhanPlaying) {
        Serial.println("[Audio] Adhan/Iqama playing, stop blocked");
        return;
    }
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio) {
        _audio->stopSong();
        _state = AUDIO_IDLE;
        _currentPriority = 0;
        _customDuration = 0;
        _loopDuration = 0;
        _lastPlayedFile = "";
        _adhanPlaying = false;
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::pause() {
    if (_adhanPlaying) return;
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio && _state == AUDIO_PLAYING) {
        _audio->pauseResume();
        _state = AUDIO_PAUSED;
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::resume() {
    if (_adhanPlaying) return;
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio && _state == AUDIO_PAUSED) {
        _audio->pauseResume();
        _state = AUDIO_PLAYING;
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::seekTo(uint16_t seconds) {
    if (_adhanPlaying) return;
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio && (_state == AUDIO_PLAYING || _state == AUDIO_PAUSED)) {
        _audio->setAudioPlayPosition(seconds);
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

uint32_t AudioManager::getAudioFileDuration() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    uint32_t ret = 0;
    if (_audio && (_state == AUDIO_PLAYING || _state == AUDIO_PAUSED)) {
        ret = _audio->getAudioFileDuration();
    }
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

uint32_t AudioManager::getAudioCurrentTime() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    uint32_t ret = 0;
    if (_audio && (_state == AUDIO_PLAYING || _state == AUDIO_PAUSED)) {
        ret = _audio->getAudioCurrentTime();
    }
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

uint8_t AudioManager::getVolume() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    uint8_t ret = 0;
    if (_audio) {
        ret = _audio->getVolume();
    }
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

void AudioManager::setRepeatMode(bool enable) {
    _repeatMode = enable;
}

bool AudioManager::getRepeatMode() const {
    return _repeatMode;
}

AudioState AudioManager::getState() {
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return _state;
    if (_state == AUDIO_PLAYING && !_audio->isRunning()) {
        audioOnStop(this);
    }
    if (_state == AUDIO_PLAYING && _customDuration > 0 &&
        (millis() - _playStartTime >= _customDuration * 1000)) {
        stop();
    }
    AudioState s = _state;
    xSemaphoreGiveRecursive(_audioMutex);
    return s;
}

const char* AudioManager::getCurrentFile() {
    return _currentFile.c_str();
}

bool AudioManager::isAdhanPlaying() const {
    return _adhanPlaying;
}

int AudioManager::getCurrentPriority() const {
    return _currentPriority;
}

bool AudioManager::isI2SReady() const {
    return _audio != nullptr;
}

void AudioManager::audioOnStop(void *userData) {
    AudioManager* self = static_cast<AudioManager*>(userData);
    if (!self) return;
    if (xSemaphoreTakeRecursive(self->_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (!self->_playlist.empty() && !self->_playlistSuspended) {
        xSemaphoreGiveRecursive(self->_audioMutex);
        self->advancePlaylist();
        return;
    }
    if (self->_repeatMode) {
        String repeatPath = normalizeAudioPath(self->_currentFile.c_str());
        if (SD.exists(repeatPath) && self->_audio->connecttoSD(repeatPath.c_str())) {
            self->_state = AUDIO_PLAYING;
            self->_playStartTime = millis();
            xSemaphoreGiveRecursive(self->_audioMutex);
            return;
        }
    }
    if (self->_repeatCount > 0) {
        self->_repeatCount--;
        String repeatPath = normalizeAudioPath(self->_currentFile.c_str());
        if (SD.exists(repeatPath) && self->_audio->connecttoSD(repeatPath.c_str())) {
            self->_state = AUDIO_PLAYING;
            self->_playStartTime = millis();
            xSemaphoreGiveRecursive(self->_audioMutex);
            return;
        }
    }
    if (self->_loopDuration > 0 && (millis() < self->_loopEndTime) && self->_lastPlayedFile.length() > 0) {
        String loopPath = normalizeAudioPath(self->_lastPlayedFile.c_str());
        if (SD.exists(loopPath) && self->_audio->connecttoSD(loopPath.c_str())) {
            self->_state = AUDIO_PLAYING;
        } else {
            self->_state = AUDIO_IDLE;
        }
        xSemaphoreGiveRecursive(self->_audioMutex);
        return;
    }
    self->_state = AUDIO_IDLE;
    self->_currentPriority = 0;
    self->_currentFile = "";
    self->_customDuration = 0;
    self->_loopDuration = 0;
    self->_repeatCount = 0;
    self->_lastPlayedFile = "";
    self->_adhanPlaying = false;
    xSemaphoreGiveRecursive(self->_audioMutex);
}

void AudioManager::loop() {
    if (_audio) {
        _audio->loop();
    }
}

void audioTask(void *pvParameters) {
    audioManager.begin();
    AudioMessage msg;
    for (;;) {
        if (xQueueReceive(audioQueue, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
                case CMD_PLAY_FILE: {
                    const char* path;
                    if (msg.param1 == 0) {
                        static char localPath[128];
                        if (xSemaphoreTake(fileBufferMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                            strlcpy(localPath, fileBuffer, sizeof(localPath));
                            xSemaphoreGive(fileBufferMutex);
                        } else {
                            Serial.println("[Audio] fileBufferMutex timeout in CMD_PLAY_FILE");
                            break;
                        }
                        path = localPath;
                    } else {
                        static char buf[64];
                        snprintf(buf, sizeof(buf), "/audio/%04d.mp3", msg.param1);
                        path = buf;
                    }
                    audioManager.playFile(path, msg.priority, msg.param2, msg.volume, msg.loopDuration, msg.repeatCount);
                    break;
                }
                case CMD_PLAY_PLAYLIST: {
                    bool respectAdhan = (msg.loopDuration & 0x1) ? true : false;
                    int pauseAfterAdhan = (msg.loopDuration >> 1) & 0xFFFF;
                    char listBuf[256];
                    if (xSemaphoreTake(fileBufferMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        strlcpy(listBuf, fileBuffer, sizeof(listBuf));
                        xSemaphoreGive(fileBufferMutex);
                    } else {
                        Serial.println("[Audio] fileBufferMutex timeout in CMD_PLAY_PLAYLIST");
                        break;
                    }
                    audioManager.playPlaylist(String(listBuf), msg.volume, respectAdhan, pauseAfterAdhan);
                    break;
                }
                case CMD_STOP:
                    audioManager.stop();
                    break;
                case CMD_SET_VOLUME:
                    audioManager.setVolume(msg.param1);
                    break;
                case CMD_PAUSE:
                    audioManager.pause();
                    break;
                case CMD_RESUME:
                    audioManager.resume();
                    break;
                case CMD_SEEK:
                    audioManager.seekTo(msg.param1);
                    break;
            }
        }
        audioManager.loop();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}
