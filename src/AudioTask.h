#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <Arduino.h>
#include <Audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum AudioCommand { CMD_PLAY_FILE=0, CMD_STOP, CMD_SET_VOLUME, CMD_PAUSE, CMD_RESUME };

struct AudioMessage {
    AudioCommand cmd;
    int param1;
    int param2;
    int priority;
    uint8_t volume;
    uint32_t loopDuration;  // seconds, 0 = play once
};

enum AudioState { AUDIO_IDLE=0, AUDIO_PLAYING, AUDIO_PAUSED };

class AudioManager {
public:
    void begin();
    bool playFile(const char* path, int priority, uint32_t duration = 0, uint8_t volume = 0, uint32_t loopDuration = 0);
    void setVolume(uint8_t vol);
    void stop();
    void pause();
    void resume();
    AudioState getState();
    const char* getCurrentFile();
    static void audioOnStop(void *userData);
private:
    Audio* _audio;
    AudioState _state;
    int _currentPriority;
    String _currentFile;
    uint32_t _playStartTime;
    uint32_t _customDuration;
    uint32_t _loopDuration = 0;
    uint32_t _loopEndTime = 0;
    String _lastPlayedFile;
    int _lastPriority = 0;
    uint8_t _lastVolume = 0;
};

void audioTask(void *pvParameters);
extern AudioManager audioManager;
extern QueueHandle_t audioQueue;
extern char fileBuffer[];
#endif