#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <Arduino.h>
#include <Audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <vector>

enum AudioCommand {
    CMD_PLAY_FILE = 0,
    CMD_STOP,
    CMD_SET_VOLUME,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_PLAY_PLAYLIST,
    CMD_SEEK
};

struct AudioMessage {
    AudioCommand cmd;
    int param1;
    int param2;
    int priority;
    uint8_t volume;
    uint32_t loopDuration;
    int repeatCount;
};

enum AudioState {
    AUDIO_IDLE = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED
};

class AudioManager {
public:
    AudioManager();
    void begin();
    bool playFile(const char* path, int priority, uint32_t duration = 0, uint8_t volume = 0, uint32_t loopDuration = 0, int repeatCount = 0);
    bool playPlaylist(const String& list, uint8_t volume, bool respectAdhan, int pauseAfterAdhan, int priority = 0);
    void advancePlaylist();
    void checkPlaylistResume();
    void setVolume(uint8_t vol);
    void stop();
    void pause();
    void resume();
    void seekTo(uint16_t seconds);
    uint32_t getAudioFileDuration();
    uint32_t getAudioCurrentTime();
    uint8_t getVolume();
    void setRepeatMode(bool enable);
    bool getRepeatMode();
    AudioState getState();
    String getCurrentFile();
    bool isI2SReady() const;
    bool isAdhanPlaying();
    int getCurrentPriority();
    void loop();
    static void audioOnStop(void *userData);
    void suspendManualPlayback();
    void resumeManualPlayback();
    void clearSuspendedState();
private:
    Audio* _audio;
    SemaphoreHandle_t _audioMutex;
    AudioState _state;
    int _currentPriority;
    String _currentFile;
    uint32_t _playStartTime;
    uint32_t _customDuration;
    uint32_t _loopDuration = 0;
    uint32_t _loopEndTime = 0;
    int _repeatCount = 0;
    String _lastPlayedFile;
    int _lastPriority = 0;
    uint8_t _lastVolume = 0;
    bool _repeatMode = false;
    bool _adhanPlaying = false;

    std::vector<String> _playlist;
    int _playlistIndex = 0;
    uint8_t _playlistVolume = 15;
    bool _respectAdhan = false;
    bool _playlistSuspended = false;
    int _pauseAfterAdhan = 0;
    unsigned long _suspendTime = 0;
    int _playlistPriority = 0;

    // Suspension tracking variables
    bool _manualSuspended = false;
    String _suspendedFile = "";
    uint32_t _suspendedPosition = 0;
    uint8_t _suspendedVolume = 0;
    uint32_t _suspendedLoopDuration = 0;
    int _suspendedRepeatCount = 0;
    int _suspendedPriority = 0;
    std::vector<String> _suspendedPlaylist;
    int _suspendedPlaylistIndex = 0;
    uint8_t _suspendedPlaylistVolume = 15;
    bool _suspendedRespectAdhan = false;
    int _suspendedPauseAfterAdhan = 0;
    bool _isPlaylistSuspendedState = false;

    unsigned long _alertEndTime = 0;
    bool _waitingForResumption = false;
    uint32_t _pendingSeekTime = 0;

    // Cache fields for thread-safe cross-core reads
    uint32_t _cachedDuration = 0;
    uint32_t _cachedCurrentTime = 0;
    uint8_t _cachedVolume = 0;
    bool _cachedIsRunning = false;
    unsigned long _lastInfoCacheUpdate = 0;
};

void audioTask(void *pvParameters);

extern AudioManager audioManager;
extern QueueHandle_t audioQueue;
extern SemaphoreHandle_t fileBufferMutex;
extern char fileBuffer[];

#endif
