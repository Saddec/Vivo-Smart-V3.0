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

bool AudioManager::playFile(const char* path, int priority, uint32_t duration, uint8_t volume, uint32_t loopDuration) {
    if (!_audio) return false;
    if (!isSDReady()) {
        Serial.printf("[Audio] SD not ready: %s\n", getLastSDError().c_str());
        return false;
    }
    if (_state == AUDIO_PLAYING && priority < _currentPriority) return false;

    if (_respectAdhan && !_playlist.empty() && priority == 3 && _state == AUDIO_PLAYING) {
        _playlistSuspended = true;
        _suspendTime = millis();
        stop();
    }

    if (_state != AUDIO_IDLE) _audio->stopSong();

    String fullPath = normalizeAudioPath(path);
    if (!SD.exists(fullPath)) {
        Serial.printf("[Audio] Missing file: %s\n", fullPath.c_str());
        _state = AUDIO_IDLE;
        return false;
    }
    if (!_audio->connecttoSD(fullPath.c_str())) {
        Serial.printf("[Audio] Cannot play: %s\n", fullPath.c_str());
        _state = AUDIO_IDLE;
        return false;
    }
    _state = AUDIO_PLAYING;
    _currentPriority = priority;
    _currentFile = path;
    _playStartTime = millis();
    _customDuration = duration;
    _loopDuration = loopDuration;
    if (loopDuration > 0) {
        _loopEndTime = millis() + (loopDuration * 1000);
        _lastPlayedFile = path;
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
    return true;
}

bool AudioManager::playPlaylist(const String& list, uint8_t volume, bool respectAdhan, int pauseAfterAdhan) {
    _playlist.clear();
    int idx = 0, last = 0;
    while (idx < list.length()) {
        if (list[idx] == ',') {
            if (idx > last) _playlist.push_back(list.substring(last, idx));
            last = idx + 1;
        }
        idx++;
    }
    if (idx > last) _playlist.push_back(list.substring(last));
    if (_playlist.empty()) return false;

    _playlistIndex = 0;
    _playlistVolume = volume;
    _respectAdhan = respectAdhan;
    _pauseAfterAdhan = pauseAfterAdhan;
    _playlistSuspended = false;

    return playFile(_playlist[0].c_str(), 0, 0, volume);
}

void AudioManager::advancePlaylist() {
    if (_playlistIndex + 1 < _playlist.size()) {
        _playlistIndex++;
        playFile(_playlist[_playlistIndex].c_str(), 0, 0, _playlistVolume);
    } else {
        _playlist.clear();
        _state = AUDIO_IDLE;
    }
}

void AudioManager::checkPlaylistResume() {
    if (_playlistSuspended && _respectAdhan && _playlist.size() > 0) {
        if (millis() - _suspendTime >= (unsigned long)(_pauseAfterAdhan * 1000)) {
            _playlistSuspended = false;
            if (_playlistIndex < _playlist.size()) {
                playFile(_playlist[_playlistIndex].c_str(), 0, 0, _playlistVolume);
            }
        }
    }
}

void AudioManager::setVolume(uint8_t vol) {
    if (_audio) _audio->setVolume(vol);
}

void AudioManager::stop() {
    if (_audio) {
        _audio->stopSong();
        _state = AUDIO_IDLE;
        _currentPriority = 0;
        _customDuration = 0;
        _loopDuration = 0;
        _lastPlayedFile = "";
    }
}

void AudioManager::pause() {
    if (_audio && _state == AUDIO_PLAYING) {
        _audio->pauseResume();
        _state = AUDIO_PAUSED;
    }
}

void AudioManager::resume() {
    if (_audio && _state == AUDIO_PAUSED) {
        _audio->pauseResume();
        _state = AUDIO_PLAYING;
    }
}

AudioState AudioManager::getState() {
    if (_state == AUDIO_PLAYING && !_audio->isRunning()) {
        audioOnStop(this);
    }
    if (_state == AUDIO_PLAYING && _customDuration > 0 &&
        (millis() - _playStartTime >= _customDuration * 1000)) {
        stop();
    }
    return _state;
}

const char* AudioManager::getCurrentFile() {
    return _currentFile.c_str();
}

void AudioManager::audioOnStop(void *userData) {
    AudioManager* self = static_cast<AudioManager*>(userData);
    if (!self) return;
    if (!self->_playlist.empty() && !self->_playlistSuspended) {
        self->advancePlaylist();
        return;
    }
    if (self->_loopDuration > 0 && (millis() < self->_loopEndTime) && self->_lastPlayedFile.length() > 0) {
        String loopPath = normalizeAudioPath(self->_lastPlayedFile.c_str());
        if (SD.exists(loopPath) && self->_audio->connecttoSD(loopPath.c_str())) {
            self->_state = AUDIO_PLAYING;
        } else {
            self->_state = AUDIO_IDLE;
        }
        return;
    }
    self->_state = AUDIO_IDLE;
    self->_currentPriority = 0;
    self->_currentFile = "";
    self->_customDuration = 0;
    self->_loopDuration = 0;
    self->_lastPlayedFile = "";
}

void audioTask(void *pvParameters) {
    audioManager.begin();
    AudioMessage msg;
    for (;;) {
        if (xQueueReceive(audioQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.cmd) {
                case CMD_PLAY_FILE: {
                    const char* path;
                    if (msg.param1 == 0) {
                        path = fileBuffer;
                    } else {
                        static char buf[64];
                        snprintf(buf, sizeof(buf), "/audio/%04d.mp3", msg.param1);
                        path = buf;
                    }
                    audioManager.playFile(path, msg.priority, msg.param2, msg.volume, msg.loopDuration);
                    break;
                }
                case CMD_PLAY_PLAYLIST: {
                    bool respectAdhan = (msg.loopDuration & 0x1) ? true : false;
                    int pauseAfterAdhan = (msg.loopDuration >> 1) & 0xFFFF;
                    audioManager.playPlaylist(String(fileBuffer), msg.volume, respectAdhan, pauseAfterAdhan);
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
            }
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}
