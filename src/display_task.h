#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include <TFT_eSPI.h>

#include "logger.h"
#include "main_task.h"
#include "task.h"

enum class State {
    CHOOSE_GIF,
    PLAY_GIF,
    SHOW_CREDITS,
};

class DisplayTask : public Task<DisplayTask>, public Logger {
    friend class Task<DisplayTask>; // Allow base Task to invoke protected run()

    public:
        DisplayTask(MainTask& main_task, const uint8_t task_core);
        virtual ~DisplayTask() {};

        void log(const char* msg) override;

    protected:
        void run();

    private:
        bool performUpdate(Stream &updateSource, size_t updateSize);
        bool updateFromFS(fs::FS &fs);
        int enumerateGifs( const char* basePath, std::vector<std::string>& out_files);
        bool isChristmas();
        void handleLogRendering();

        void log(String msg);

        TFT_eSPI tft_ = TFT_eSPI();
        MainTask& main_task_;
        QueueHandle_t log_queue_;
        QueueHandle_t event_queue_;

        bool show_log_ = false;
        bool message_visible_ = false;
        char current_message_[200];
        uint32_t last_message_millis_ = UINT32_MAX;

};
