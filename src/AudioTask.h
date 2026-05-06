#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <Arduino.h>
#include <Audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum AudioCommand {
    CMD_PLAY_FILE = 0,
    CMD_STOP,
    CMD_SET_VOLUME,
    CMD_PAUSE,
    CMD_RESUME
};

struct AudioMessage {
    AudioCommand cmd;
    int param1;       // 0 = use fileBuffer, else file number
    int param2;       // duration in seconds
    int priority;     // 0=normal, 1=alert, 2=iqama, 3=adhan
    uint8_t volume;   // volume 0-30 (0 means use default priority-based volume)
};

enum AudioState {
    AUDIO_IDLE = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED
};

class AudioManager {
public:
    void begin();
    bool playFile(const char* path, int priority, uint32_t duration = 0, uint8_t volume = 0);
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
};

void audioTask(void *pvParameters);

extern AudioManager audioManager;
extern QueueHandle_t audioQueue;
extern char fileBuffer[];

#endif