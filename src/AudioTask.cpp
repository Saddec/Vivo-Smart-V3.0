#include "AudioTask.h"
#include "SDManager.h"
#include <SD.h>
#include "Audio.h"
#include "EventLogger.h"

#define I2S_BCLK 16
#define I2S_LRCK 17
#define I2S_DOUT 18
static const uint8_t AUDIO_UI_MAX_VOLUME = 30;
static const uint8_t AUDIO_POWER_SAFE_MAX_VOLUME = 30;

static String normalizeAudioPath(const char* path) {
    String fullPath = String(path);
    fullPath.trim();
    if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
    fullPath.replace("//", "/");
    return fullPath;
}

static uint8_t clampPowerSafeVolume(uint8_t volume) {
    if (volume > AUDIO_UI_MAX_VOLUME) volume = AUDIO_UI_MAX_VOLUME;
    if (volume > AUDIO_POWER_SAFE_MAX_VOLUME) return AUDIO_POWER_SAFE_MAX_VOLUME;
    return volume;
}

AudioManager audioManager;

AudioManager::AudioManager() 
    : _audio(nullptr), _state(AUDIO_IDLE), _currentPriority(0), _currentFile(""),
      _playStartTime(0), _customDuration(0), _loopDuration(0), _loopEndTime(0),
      _repeatCount(0), _lastPriority(0), _lastVolume(0), _repeatMode(false),
      _adhanPlaying(false), _playlistIndex(0), _playlistVolume(15),
      _respectAdhan(false), _playlistSuspended(false), _pauseAfterAdhan(0),
      _suspendTime(0), _playlistPriority(0),
      _manualSuspended(false), _suspendedFile(""), _suspendedPosition(0), _suspendedVolume(0),
      _suspendedLoopDuration(0), _suspendedRepeatCount(0), _suspendedPriority(0),
      _suspendedPlaylistIndex(0), _suspendedPlaylistVolume(15), _suspendedRespectAdhan(false),
      _suspendedPauseAfterAdhan(0), _isPlaylistSuspendedState(false),
      _alertEndTime(0), _waitingForResumption(false), _pendingSeekTime(0),
      _cachedDuration(0), _cachedCurrentTime(0), _cachedVolume(0), _cachedIsRunning(false),
      _lastInfoCacheUpdate(0)
{
    _audioMutex = xSemaphoreCreateRecursiveMutex();
}

void AudioManager::begin() {
    if (!initSDCard(true)) {
        Serial.printf("[Audio] SD Card failed: %s\n", getLastSDError().c_str());
    } else {
        Serial.println("[Audio] SD Card OK.");
    }
    _audio = new Audio();
    _audio->setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
    _audio->setVolume(10); // Map initial volume 15 to library scale: (15 * 21) / 30 = 10

    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) == pdTRUE) {
        _state = AUDIO_IDLE;
        _currentPriority = 0;
        _currentFile = "";
        _playStartTime = 0;
        _customDuration = 0;
        _loopDuration = 0;
        _loopEndTime = 0;
        _cachedDuration = 0;
        _cachedCurrentTime = 0;
        _cachedVolume = 15;
        _cachedIsRunning = false;
        _lastInfoCacheUpdate = 0;
        xSemaphoreGiveRecursive(_audioMutex);
    }
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

    _waitingForResumption = false;
    if (priority == 0) {
        clearSuspendedState();
    }

    if ((_state == AUDIO_PLAYING || _state == AUDIO_PAUSED) && _currentPriority == 0 && priority > 0) {
        _playlist.clear();
        clearSuspendedState();
    }

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
        LOG_E("AUDIO", "Missing audio file: %s", fullPath.c_str());
        _state = AUDIO_IDLE;
        _adhanPlaying = false;
        _cachedIsRunning = false;
        _cachedDuration = 0;
        _cachedCurrentTime = 0;
        xSemaphoreGiveRecursive(_audioMutex);
        return false;
    }
    if (!_audio->connecttoSD(fullPath.c_str())) {
        Serial.printf("[Audio] Cannot play: %s\n", fullPath.c_str());
        LOG_E("AUDIO", "Cannot play audio file: %s", fullPath.c_str());
        _state = AUDIO_IDLE;
        _adhanPlaying = false;
        _cachedIsRunning = false;
        _cachedDuration = 0;
        _cachedCurrentTime = 0;
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

    uint8_t targetVol = 15;
    if (volume > 0) targetVol = volume;
    else if (priority == 3) targetVol = 30;
    else if (priority == 2) targetVol = 30;
    uint8_t requestedVol = targetVol;
    targetVol = clampPowerSafeVolume(targetVol);
    if (requestedVol != targetVol) {
        LOG_W("AUDIO", "Requested volume %d limited to %d for power stability", requestedVol, targetVol);
    }
    _audio->setVolume((targetVol * 21) / 30);
    
    // Immediately update cache under mutex
    _cachedVolume = targetVol;
    _cachedIsRunning = true;
    _cachedDuration = 0;
    _cachedCurrentTime = 0;
    _lastInfoCacheUpdate = 0;

    LOG_AUD("AUDIO", "Started playing file: %s (Priority: %d, Vol: %d)", fullPath.c_str(), priority, targetVol);

    xSemaphoreGiveRecursive(_audioMutex);
    return true;
}

bool AudioManager::playPlaylist(const String& list, uint8_t volume, bool respectAdhan, int pauseAfterAdhan, int priority) {
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
    _playlistPriority = priority;
    LOG_AUD("AUDIO", "Started playlist playback with %d files", (int)_playlist.size());
    xSemaphoreGiveRecursive(_audioMutex);

    return playFile(_playlist[0].c_str(), priority, 0, volume);
}

void AudioManager::advancePlaylist() {
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_playlistIndex + 1 < (int)_playlist.size()) {
        _playlistIndex++;
        String nextFile = _playlist[_playlistIndex];
        int priority = _playlistPriority;
        xSemaphoreGiveRecursive(_audioMutex);
        playFile(nextFile.c_str(), priority, 0, _playlistVolume);
    } else {
        _playlist.clear();
        _state = AUDIO_IDLE;
        if (_currentPriority == 1) {
            _alertEndTime = millis();
            _waitingForResumption = true;
        }
        _currentPriority = 0;
        LOG_AUD("AUDIO", "Playlist playback completed");
        extern String currentAudioDescription;
        currentAudioDescription = "";
        xSemaphoreGiveRecursive(_audioMutex);
    }
}

void AudioManager::checkPlaylistResume() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (_playlistSuspended && _respectAdhan && !_playlist.empty()) {
        if (millis() - _suspendTime >= (unsigned long)(_pauseAfterAdhan * 1000)) {
            _playlistSuspended = false;
            if (_playlistIndex < (int)_playlist.size()) {
                String resumeFile = _playlist[_playlistIndex];
                int priority = _playlistPriority;
                LOG_AUD("AUDIO", "Resuming suspended playlist");
                xSemaphoreGiveRecursive(_audioMutex);
                playFile(resumeFile.c_str(), priority, 0, _playlistVolume);
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
    if (_audio) {
        uint8_t requestedVol = vol;
        vol = clampPowerSafeVolume(vol);
        _audio->setVolume((vol * 21) / 30);
        _cachedVolume = vol;
        if (requestedVol != vol) {
            LOG_W("AUDIO", "Requested volume %d limited to %d for power stability", requestedVol, vol);
        }
        LOG_AUD("AUDIO", "Volume set to %d", vol);
    }
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
        _currentFile = "";
        _customDuration = 0;
        _loopDuration = 0;
        _lastPlayedFile = "";
        _adhanPlaying = false;
        _cachedIsRunning = false;
        _cachedDuration = 0;
        _cachedCurrentTime = 0;
        _pendingSeekTime = 0;
        
        _manualSuspended = false;
        _suspendedFile = "";
        _suspendedPosition = 0;
        _suspendedVolume = 0;
        _suspendedLoopDuration = 0;
        _suspendedRepeatCount = 0;
        _suspendedPriority = 0;
        _suspendedPlaylist.clear();
        _suspendedPlaylistIndex = 0;
        _suspendedPlaylistVolume = 15;
        _suspendedRespectAdhan = false;
        _suspendedPauseAfterAdhan = 0;
        _isPlaylistSuspendedState = false;
        _waitingForResumption = false;
        extern String currentAudioDescription;
        currentAudioDescription = "";
        LOG_AUD("AUDIO", "Playback stopped");
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::pause() {
    if (_adhanPlaying) return;
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio && _state == AUDIO_PLAYING) {
        _audio->pauseResume();
        _state = AUDIO_PAUSED;
        _cachedIsRunning = false;
        LOG_AUD("AUDIO", "Playback paused");
    }
    xSemaphoreGiveRecursive(_audioMutex);
}

void AudioManager::resume() {
    if (_adhanPlaying) return;
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (_audio && _state == AUDIO_PAUSED) {
        _audio->pauseResume();
        _state = AUDIO_PLAYING;
        _cachedIsRunning = true;
        LOG_AUD("AUDIO", "Playback resumed");
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
    uint32_t ret = _cachedDuration;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

uint32_t AudioManager::getAudioCurrentTime() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    uint32_t ret = _cachedCurrentTime;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

uint8_t AudioManager::getVolume() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    uint8_t ret = _cachedVolume;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

void AudioManager::setRepeatMode(bool enable) {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _repeatMode = enable;
        xSemaphoreGiveRecursive(_audioMutex);
    }
}

bool AudioManager::getRepeatMode() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return _repeatMode;
    bool ret = _repeatMode;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

AudioState AudioManager::getState() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return _state;
    AudioState s = _state;
    xSemaphoreGiveRecursive(_audioMutex);
    return s;
}

String AudioManager::getCurrentFile() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return "";
    String ret = _currentFile;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

bool AudioManager::isAdhanPlaying() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return _adhanPlaying;
    bool ret = _adhanPlaying;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
}

int AudioManager::getCurrentPriority() {
    if (xSemaphoreTakeRecursive(_audioMutex, pdMS_TO_TICKS(10)) != pdTRUE) return _currentPriority;
    int ret = _currentPriority;
    xSemaphoreGiveRecursive(_audioMutex);
    return ret;
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
            self->_cachedIsRunning = true;
            self->_cachedDuration = 0;
            self->_cachedCurrentTime = 0;
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
            self->_cachedIsRunning = true;
            self->_cachedDuration = 0;
            self->_cachedCurrentTime = 0;
            xSemaphoreGiveRecursive(self->_audioMutex);
            return;
        }
    }
    if (self->_loopDuration > 0 && (millis() < self->_loopEndTime) && self->_lastPlayedFile.length() > 0) {
        String loopPath = normalizeAudioPath(self->_lastPlayedFile.c_str());
        if (SD.exists(loopPath) && self->_audio->connecttoSD(loopPath.c_str())) {
            self->_state = AUDIO_PLAYING;
            self->_cachedIsRunning = true;
            self->_cachedDuration = 0;
            self->_cachedCurrentTime = 0;
        } else {
            self->_state = AUDIO_IDLE;
            self->_cachedIsRunning = false;
            self->_cachedDuration = 0;
            self->_cachedCurrentTime = 0;
            extern String currentAudioDescription;
            currentAudioDescription = "";
        }
        xSemaphoreGiveRecursive(self->_audioMutex);
        return;
    }
    // Manual resumption disabled: do not trigger waiting for resumption on alert end.
    /*
    if (self->_currentPriority == 1 && self->_manualSuspended) {
        self->_alertEndTime = millis();
        self->_waitingForResumption = true;
        Serial.println("[Audio] Priority 1 alert finished. Scheduled resumption in 30s.");
    }
    */
    self->_state = AUDIO_IDLE;
    self->_currentPriority = 0;
    self->_currentFile = "";
    self->_customDuration = 0;
    self->_loopDuration = 0;
    self->_repeatCount = 0;
    self->_lastPlayedFile = "";
    self->_adhanPlaying = false;
    self->_cachedIsRunning = false;
    self->_cachedDuration = 0;
    self->_cachedCurrentTime = 0;
    self->_pendingSeekTime = 0;
    extern String currentAudioDescription;
    currentAudioDescription = "";
    xSemaphoreGiveRecursive(self->_audioMutex);
}

void AudioManager::loop() {
    if (!_audio) return;
    lockSD();
    _audio->loop();
    unlockSD();

    if (xSemaphoreTakeRecursive(_audioMutex, 0) == pdTRUE) {
        _cachedIsRunning = _audio->isRunning();
        unsigned long nowMs = millis();
        if ((_state == AUDIO_PLAYING || _state == AUDIO_PAUSED) && (nowMs - _lastInfoCacheUpdate >= 500 || _lastInfoCacheUpdate == 0)) {
            _cachedDuration = _audio->getAudioFileDuration();
            _cachedCurrentTime = _audio->getAudioCurrentTime();
            _lastInfoCacheUpdate = nowMs;
        } else if (_state != AUDIO_PLAYING && _state != AUDIO_PAUSED) {
            _cachedDuration = 0;
            _cachedCurrentTime = 0;
            _lastInfoCacheUpdate = 0;
        }
        // _cachedVolume is maintained in the 0-30 scale; do not overwrite with library 0-21 volume.

        if (_state == AUDIO_PLAYING && _pendingSeekTime > 0 && _cachedDuration > 0) {
            Serial.printf("[Audio] Applying pending seek to %u seconds\n", _pendingSeekTime);
            if (_audio) {
                _audio->setAudioPlayPosition(_pendingSeekTime);
            }
            _pendingSeekTime = 0;
        }

        // Manual resumption disabled: check for waitingForResumption removed.
        /*
        if (_waitingForResumption && _manualSuspended && _state == AUDIO_IDLE) {
            if (millis() - _alertEndTime >= 30000) {
                Serial.println("[Audio] 30 seconds passed. Triggering resumption of manual playback.");
                _waitingForResumption = false;
                resumeManualPlayback();
            }
        }
        */

        if (_state == AUDIO_PLAYING && !_cachedIsRunning) {
            audioOnStop(this);
        }

        if (_state == AUDIO_PLAYING && _customDuration > 0 &&
            (millis() - _playStartTime >= _customDuration * 1000)) {
            stop();
        }
        xSemaphoreGiveRecursive(_audioMutex);
    }

    checkPlaylistResume();
}

void AudioManager::suspendManualPlayback() {
    if (_manualSuspended) {
        Serial.println("[Audio] Playback already suspended, skipping double-suspend");
        return;
    }
    _manualSuspended = true;
    _suspendedFile = _currentFile;
    _suspendedPosition = _cachedCurrentTime;
    _suspendedVolume = _cachedVolume;
    _suspendedLoopDuration = _loopDuration;
    _suspendedRepeatCount = _repeatCount;
    _suspendedPriority = _currentPriority;
    
    if (!_playlist.empty()) {
        _isPlaylistSuspendedState = true;
        _suspendedPlaylist = _playlist;
        _suspendedPlaylistIndex = _playlistIndex;
        _suspendedPlaylistVolume = _playlistVolume;
        _suspendedRespectAdhan = _respectAdhan;
        _suspendedPauseAfterAdhan = _pauseAfterAdhan;
        Serial.printf("[Audio] Suspended manual playlist at index %d, file: %s, pos: %u s\n", 
            _playlistIndex, _currentFile.c_str(), _suspendedPosition);
    } else {
        _isPlaylistSuspendedState = false;
        _suspendedPlaylist.clear();
        Serial.printf("[Audio] Suspended manual file: %s, pos: %u s\n", 
            _currentFile.c_str(), _suspendedPosition);
    }
}

void AudioManager::resumeManualPlayback() {
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    if (!_manualSuspended) {
        _waitingForResumption = false;
        xSemaphoreGiveRecursive(_audioMutex);
        return;
    }

    Serial.printf("[Audio] Resuming manual playback. File: %s, Position: %u s\n", 
        _suspendedFile.c_str(), _suspendedPosition);

    _waitingForResumption = false;
    
    String fileToPlay = _suspendedFile;
    uint32_t posToSeek = _suspendedPosition;
    uint8_t volToSet = _suspendedVolume;
    uint32_t loopDur = _suspendedLoopDuration;
    int repCount = _suspendedRepeatCount;
    bool isPlaylist = _isPlaylistSuspendedState;
    
    std::vector<String> playlistToRestore = _suspendedPlaylist;
    int playlistIdx = _suspendedPlaylistIndex;
    uint8_t playlistVol = _suspendedPlaylistVolume;
    bool respAdhan = _suspendedRespectAdhan;
    int pauseAdhan = _suspendedPauseAfterAdhan;

    _manualSuspended = false;
    _suspendedFile = "";
    _suspendedPosition = 0;
    _suspendedPlaylist.clear();

    if (isPlaylist && !playlistToRestore.empty()) {
        _playlist = std::move(playlistToRestore);
        _playlistIndex = playlistIdx;
        _playlistVolume = playlistVol;
        _respectAdhan = respAdhan;
        _pauseAfterAdhan = pauseAdhan;
        _playlistSuspended = false;
        _playlistPriority = 0;
        
        if (playlistIdx >= 0 && playlistIdx < (int)_playlist.size()) {
            _pendingSeekTime = posToSeek;
            xSemaphoreGiveRecursive(_audioMutex);
            playFile(_playlist[playlistIdx].c_str(), 0, 0, playlistVol);
        } else {
            xSemaphoreGiveRecursive(_audioMutex);
        }
    } else {
        if (fileToPlay.length() > 0) {
            _pendingSeekTime = posToSeek;
            xSemaphoreGiveRecursive(_audioMutex);
            playFile(fileToPlay.c_str(), 0, 0, volToSet, loopDur, repCount);
        } else {
            xSemaphoreGiveRecursive(_audioMutex);
        }
    }
}

void AudioManager::clearSuspendedState() {
    if (xSemaphoreTakeRecursive(_audioMutex, portMAX_DELAY) != pdTRUE) return;
    _manualSuspended = false;
    _suspendedFile = "";
    _suspendedPosition = 0;
    _suspendedVolume = 0;
    _suspendedLoopDuration = 0;
    _suspendedRepeatCount = 0;
    _suspendedPriority = 0;
    _suspendedPlaylist.clear();
    _suspendedPlaylistIndex = 0;
    _suspendedPlaylistVolume = 15;
    _suspendedRespectAdhan = false;
    _suspendedPauseAfterAdhan = 0;
    _isPlaylistSuspendedState = false;
    _waitingForResumption = false;
    _pendingSeekTime = 0;
    xSemaphoreGiveRecursive(_audioMutex);
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
                        static char localPath[256];
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
                    audioManager.playPlaylist(String(listBuf), msg.volume, respectAdhan, pauseAfterAdhan, msg.priority);
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
