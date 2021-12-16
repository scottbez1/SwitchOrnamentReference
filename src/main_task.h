#pragma once

#include <Arduino.h>

#include <AceButton.h>

#include "event.h"
#include "logger.h"
#include "task.h"

class MainTask : public Task<MainTask>, public ace_button::IEventHandler {
    friend class Task<MainTask>; // Allow base Task to invoke protected run()

    public:
        MainTask(const uint8_t task_core);
        virtual ~MainTask();

        void setConfig(const char* wifi_ssid, const char* wifi_password, const char* timezone);
        bool getLocalTime(tm* t);
        void setLogger(Logger* logger);
        void setOtaEnabled(bool enabled);
        void registerEventQueue(QueueHandle_t queue);

        void handleEvent(ace_button::AceButton* button, uint8_t event_type, uint8_t button_state) override;

    protected:
        void run();

    private:

        void log(const char* message);
        void log(String message);

        void publishEvent(Event event);
        
        SemaphoreHandle_t semaphore_;

        String wifi_ssid_;
        String wifi_password_;
        String timezone_;

        bool ntp_synced_ = false;

        Logger* logger_ = nullptr;

        std::vector<QueueHandle_t> event_queues_;
};
