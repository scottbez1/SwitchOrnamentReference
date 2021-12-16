#include "main_task.h"

#include <AceButton.h>
#include <ArduinoOTA.h>
#include <lwip/apps/sntp.h>
#include <time.h>
#include <WiFi.h>

#include "semaphore_guard.h"

#define TASK_NOTIFY_SET_CONFIG (1 << 0)

#define MDNS_NAME "switchOrnament"
#define OTA_PASSWORD "hunter2"

#define PIN_LEFT_BUTTON 32
#define PIN_RIGHT_BUTTON 26

using namespace ace_button;

MainTask::MainTask(const uint8_t task_core) : Task{"Main", 8192, 1, task_core}, semaphore_(xSemaphoreCreateMutex()) {
    assert(semaphore_ != NULL);
    xSemaphoreGive(semaphore_);
}

MainTask::~MainTask() {
    if (semaphore_ != NULL) {
        vSemaphoreDelete(semaphore_);
    }
}

void MainTask::run() {
    WiFi.mode(WIFI_STA);

    AceButton left_button(PIN_LEFT_BUTTON, 1, BUTTON_ID_LEFT);
    AceButton right_button(PIN_RIGHT_BUTTON, 1, BUTTON_ID_RIGHT);

    pinMode(PIN_LEFT_BUTTON, INPUT_PULLUP);
    pinMode(PIN_RIGHT_BUTTON, INPUT_PULLUP);

    ButtonConfig* config = ButtonConfig::getSystemButtonConfig();
    config->setIEventHandler(this);
    
    ArduinoOTA
        .onStart([this]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "(flash)";
            else // U_SPIFFS
                type = "(filesystem)";

            // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
            log("Start OTA " + type);
        })
        .onEnd([this]() {
            log("OTA End");
        })
        .onProgress([this](unsigned int progress, unsigned int total) {
            static uint32_t last_progress;
            if (millis() - last_progress > 1000) {
                log("OTA Progress: " + String((int)(progress * 100 / total)) + "%");
                last_progress = millis();
            }
        })
        .onError([this](ota_error_t error) {
            log("Error[%u]: " + String(error));
            if (error == OTA_AUTH_ERROR) log("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) log("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) log("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) log("Receive Failed");
            else if (error == OTA_END_ERROR) log("End Failed");
        });
    ArduinoOTA.setHostname(MDNS_NAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    wl_status_t wifi_status = WL_DISCONNECTED;
    while (1) {
        uint32_t notify_value = 0;
        if (xTaskNotifyWait(0, ULONG_MAX, &notify_value, 0) == pdTRUE) {
            if (notify_value && TASK_NOTIFY_SET_CONFIG) {
                String wifi_ssid, wifi_password, timezone;
                {
                    SemaphoreGuard lock(semaphore_);
                    wifi_ssid = wifi_ssid_;
                    wifi_password = wifi_password_;
                    timezone = timezone_;
                }
                setenv("TZ", timezone.c_str(), 1);
                tzset();

                char buf[200];
                snprintf(buf, sizeof(buf), "Connecting to %s...", wifi_ssid.c_str());
                log(buf);
                WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
            }
        }

        wl_status_t new_status = WiFi.status();
        if (new_status != wifi_status) {
            char buf[200];
            snprintf(buf, sizeof(buf), "Wifi status changed to %d\n", new_status);
            log(buf);
            if (new_status == WL_CONNECTED) {
                snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
                log(buf);

                delay(100);
                // Sync SNTP
                sntp_setoperatingmode(SNTP_OPMODE_POLL);

                char server[] = "time.nist.gov"; // sntp_setservername takes a non-const char*, so use a non-const variable to avoid warning
                sntp_setservername(0, server);
                sntp_init();
            }

            wifi_status = new_status;
        }


        time_t now = 0;
        bool ntp_just_synced = false;
        {
            SemaphoreGuard lock(semaphore_);
            if (!ntp_synced_) {
                // Check if NTP has synced yet
                time(&now);
                if (now > 1625099485) {
                    ntp_just_synced = true;
                    ntp_synced_ = true;
                }
            }
        }

        if (ntp_just_synced) {
            // We do this separately from above to avoid deadlock: log() requires semaphore_ and we're non-reentrant-locking
            char buf[200];
            strftime(buf, sizeof(buf), "Got time: %Y-%m-%d %H:%M:%S", localtime(&now));
            Serial.printf("%s\n", buf);
            log(buf);
        }

        ArduinoOTA.handle();
        left_button.check();
        right_button.check();
        delay(1);
    }
}

void MainTask::setConfig(const char* wifi_ssid, const char* wifi_password, const char* timezone) {
    {
        SemaphoreGuard lock(semaphore_);
        wifi_ssid_ = String(wifi_ssid);
        wifi_password_ = String(wifi_password);
        timezone_ = String(timezone);
    }
    xTaskNotify(getHandle(), TASK_NOTIFY_SET_CONFIG, eSetBits);
}

bool MainTask::getLocalTime(tm* t) {
    SemaphoreGuard lock(semaphore_);
    if (!ntp_synced_) {
        return false;
    }
    time_t now = 0;
    time(&now);
    localtime_r(&now, t);
    return true;
}

void MainTask::setLogger(Logger* logger) {
    SemaphoreGuard lock(semaphore_);
    logger_ = logger;
}

void MainTask::setOtaEnabled(bool enabled) {
    if (enabled) {
        ArduinoOTA.begin();
    } else {
        ArduinoOTA.end();
    }
}

void MainTask::log(const char* message) {
    SemaphoreGuard lock(semaphore_);
    if (logger_ != nullptr) {
        logger_->log(message);
    } else {
        Serial.println(message);
    }
}

void MainTask::log(String message) {
    log(message.c_str());
}

void MainTask::registerEventQueue(QueueHandle_t queue) {
    SemaphoreGuard lock(semaphore_);
    event_queues_.push_back(queue);
}

void MainTask::publishEvent(Event event) {
    SemaphoreGuard lock(semaphore_);
    for (QueueHandle_t queue : event_queues_) {
        xQueueSend(queue, &event, 0);
    }
}

void MainTask::handleEvent(AceButton* button, uint8_t event_type, uint8_t button_state) {
    Event event = {
        .type = EventType::BUTTON,
        {
            .button = {
                .button_id = button->getId(),
                .event = event_type,
            },
        }
    };
    publishEvent(event);
}
