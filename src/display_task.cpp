#include "display_task.h"

#include <AceButton.h>
#include <SD_MMC.h>
#include <TFT_eSPI.h>
#include <Update.h>
#include <WiFi.h>

#include <json11.hpp>

#include "gif_player.h"

using namespace json11;

#define PIN_LCD_BACKLIGHT 27

#define PIN_SD_DAT1 4
#define PIN_SD_DAT2 12

DisplayTask::DisplayTask(MainTask& main_task, const uint8_t task_core) : Task{"Display", 8192, 1, task_core}, Logger(), main_task_(main_task) {
    log_queue_ = xQueueCreate(10, sizeof(std::string *));
    assert(log_queue_ != NULL);

    event_queue_ = xQueueCreate(10, sizeof(Event));
    assert(event_queue_ != NULL);
}

int DisplayTask::enumerateGifs(const char* basePath, std::vector<std::string>& out_files) {
    int amount = 0;
    File GifRootFolder = SD_MMC.open(basePath);
    if(!GifRootFolder){
        log_n("Failed to open directory");
        return 0;
    }

    if(!GifRootFolder.isDirectory()){
        log_n("Not a directory");
        return 0;
    }

    File file = GifRootFolder.openNextFile();

    while( file ) {
        if(!file.isDirectory()) {
            out_files.push_back( file.name() );
            amount++;
            file.close();
        }
        file = GifRootFolder.openNextFile();
    }
    GifRootFolder.close();
    log_n("Found %d GIF files", amount);
    return amount;
}


// perform the actual update from a given stream
bool DisplayTask::performUpdate(Stream &updateSource, size_t updateSize) {
   if (Update.begin(updateSize)) {      
      size_t written = Update.writeStream(updateSource);
      if (written == updateSize) {
         Serial.println("Written : " + String(written) + " successfully");
      }
      else {
         Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
      }
      if (Update.end()) {
         Serial.println("OTA done!");
         if (Update.isFinished()) {
            Serial.println("Update successfully completed. Rebooting.");
            tft_.fillScreen(TFT_BLACK);
            tft_.drawString("Update successful!", 0, 0);
            return true;
         }
         else {
            Serial.println("Update not finished? Something went wrong!");
            tft_.fillScreen(TFT_BLACK);
            tft_.drawString("Update error: unknown", 0, 0);
         }
      }
      else {
        uint8_t error = Update.getError();
         Serial.println("Error Occurred. Error #: " + String(error));
        tft_.fillScreen(TFT_BLACK);
        tft_.drawString("Update error: " + String(error), 0, 0);
      }

   }
   else
   {
      Serial.println("Not enough space to begin OTA");
        tft_.fillScreen(TFT_BLACK);
        tft_.drawString("Not enough space", 0, 0);
   }
   return false;
}

// check given FS for valid firmware.bin and perform update if available
bool DisplayTask::updateFromFS(fs::FS &fs) {
  tft_.fillScreen(TFT_BLACK);
  tft_.setTextDatum(TL_DATUM);

   File updateBin = fs.open("/firmware.bin");
   if (updateBin) {
      if(updateBin.isDirectory()){
         Serial.println("Error, firmware.bin is not a file");
         updateBin.close();
         return false;
      }

      size_t updateSize = updateBin.size();

      bool update_successful = false;
      if (updateSize > 0) {
          Serial.println("Try to start update");
          digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
          tft_.fillScreen(TFT_BLACK);
          tft_.drawString("Starting update...", 0, 0);
          delay(1000);
         update_successful = performUpdate(updateBin, updateSize);
      }
      else {
         Serial.println("Error, file is empty");
      }

      updateBin.close();
      fs.remove("/firmware.bin");

      // Leave some time to read the update result message
      delay(5000);
      return update_successful;
   }
   else {
      Serial.println("No firmware.bin at sd root");
      return false;
   }
}

void DisplayTask::run() {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    pinMode(PIN_SD_DAT1, INPUT_PULLUP);
    pinMode(PIN_SD_DAT2, INPUT_PULLUP);

    tft_.begin();
#ifdef USE_DMA
    tft_.initDMA();
#endif
    tft_.setRotation(1);
    tft_.fillScreen(TFT_BLACK);

    bool isblinked = false;
    while(! SD_MMC.begin("/sdcard", false) ) {
        digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
        log_n("SD Card mount failed!");
        isblinked = !isblinked;
        if( isblinked ) {
            tft_.setTextColor( TFT_WHITE, TFT_BLACK );
        } else {
            tft_.setTextColor( TFT_BLACK, TFT_WHITE );
        }
        tft_.setTextDatum(TC_DATUM);
        tft_.drawString( "INSERT SD", tft_.width()/2, tft_.height()/2 );

        delay( 300 );
    }

    log_n("SD Card mounted!");

    if (updateFromFS(SD_MMC)) {
        ESP.restart();
    }

    // #####################################################
    // CHANGES ABOVE THIS LINE MAY BREAK FIRMWARE UPDATES!!!
    // #####################################################

    main_task_.setLogger(this);

    // Load config from SD card
    File configFile = SD_MMC.open("/config.json");
    if (configFile) {
        if(configFile.isDirectory()){
            log("Error, config.json is not a file");
        } else {
            char data[512];
            size_t data_len = configFile.readBytes(data, sizeof(data) - 1);
            data[data_len] = 0;

            std::string err;
            Json json = Json::parse(data, err);
            if (err.empty()) {
                show_log_ = json["show_log"].bool_value();
                const char* ssid = json["ssid"].string_value().c_str();
                const char* password = json["password"].string_value().c_str();
                Serial.printf("Wifi info: %s %s\n", ssid, password);

                const char* tz = json["timezone"].string_value().c_str();
                Serial.printf("Timezone: %s\n", tz);

                main_task_.setConfig(ssid, password, tz);
            } else {
                log("Error parsing wifi credentials! " + String(err.c_str()));
            }
        }
        configFile.close();
    } else {
        log("Missing config file!");
    }

    // Delay to avoid brownout while wifi is starting
    delay(500);

    GifPlayer::begin(&tft_);

    if (GifPlayer::start("/gifs/boot.gif")) {
        GifPlayer::play_frame(nullptr);
        delay(50);
        digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
        delay(200);
        while (GifPlayer::play_frame(nullptr)) {
            yield();
        }
        digitalWrite(PIN_LCD_BACKLIGHT, LOW);
        delay(500);
        GifPlayer::stop();
    }

    std::vector<std::string> main_gifs;
    std::vector<std::string> christmas_gifs;

    int num_main_gifs = enumerateGifs( "/gifs/main", main_gifs);
    int num_christmas_gifs = enumerateGifs( "/gifs/christmas", christmas_gifs);
    int current_file = -1;
    const char* current_file_name = "";
    uint32_t minimum_loop_duration = 0;
    uint32_t start_millis = UINT32_MAX;

    bool last_christmas; // I gave you my heart...

    main_task_.registerEventQueue(event_queue_);

    State state = State::CHOOSE_GIF;
    int frame_delay = 0;
    uint32_t last_frame = 0;
    while (1) {
        bool left_button = false;
        bool right_button = false;
        Event event;
        if (xQueueReceive(event_queue_, &event, 0)) {
            switch (event.type) {
                case EventType::BUTTON:
                    if (event.button.event == ace_button::AceButton::kEventPressed) {
                        if (event.button.button_id == BUTTON_ID_LEFT) {
                            left_button = true;
                        } else if (event.button.button_id == BUTTON_ID_RIGHT) {
                            right_button = true;
                        }
                    }
                    break;
            }
        }
        handleLogRendering();
        switch (state) {
            case State::CHOOSE_GIF:
                Serial.println("Choose gif");
                if (millis() - start_millis > minimum_loop_duration) {
                    // Only change the file if we've exceeded the minimum loop duration
                    if (isChristmas()) {
                        if (num_christmas_gifs > 0) {
                            current_file_name = christmas_gifs[current_file++ % num_christmas_gifs].c_str();
                            minimum_loop_duration = 30000;
                            Serial.printf("Chose christmas gif: %s\n", current_file_name);
                        } else {
                            continue;
                        }
                    } else {
                        if (num_main_gifs > 0) {
                            int next_file = current_file;
                            while (num_main_gifs > 1 && next_file == current_file) {
                                next_file = random(num_main_gifs);
                            }
                            current_file = next_file;
                            current_file_name = main_gifs[current_file].c_str();
                            minimum_loop_duration = 0;
                            Serial.printf("Chose gif: %s\n", current_file_name);
                        } else {
                            continue;
                        }
                    }
                    start_millis = millis();
                }
                if (!GifPlayer::start(current_file_name)) {
                    continue;
                }
                last_frame = millis();
                GifPlayer::play_frame(&frame_delay);
                delay(50);
                digitalWrite(PIN_LCD_BACKLIGHT, HIGH);
                state = State::PLAY_GIF;
                break;
            case State::PLAY_GIF: {
                if (right_button) {
                    GifPlayer::stop();
                    int center = tft_.width()/2;
                    tft_.fillScreen(TFT_BLACK);
                    tft_.setTextSize(2);
                    tft_.setTextDatum(TC_DATUM);
                    tft_.drawString("Merry Christmas!", center, 10);
                    tft_.setTextSize(1);
                    tft_.drawString("Designed and handmade", center, 50);
                    tft_.drawString("by Scott Bezek", center, 60);
                    tft_.drawString("Oakland, 2021", center, 80);

                    if (WiFi.status() == WL_CONNECTED) {
                        tft_.setTextDatum(BL_DATUM);
                        tft_.drawString(String("IP: ") + WiFi.localIP().toString(), 5, tft_.height());
                    }
                    main_task_.setOtaEnabled(true);
                    delay(200);
                    state = State::SHOW_CREDITS;
                    break;
                }
                bool is_christmas = isChristmas();
                bool christmas_changed = false;
                if (is_christmas != last_christmas) {
                    last_christmas = is_christmas;
                    christmas_changed = true;
                }

                if (left_button || christmas_changed) {
                    // Force select new gif, even if we hadn't met the minimum loop duration yet
                    minimum_loop_duration = 0;
                    GifPlayer::stop();
                    state = State::CHOOSE_GIF;
                    break;
                }
                uint32_t time_since_last_frame = millis() - last_frame;
                if (time_since_last_frame > frame_delay) {
                    // Time for the next frame; play it
                    last_frame = millis();
                    if (!GifPlayer::play_frame(&frame_delay)) {
                        GifPlayer::stop();
                        state = State::CHOOSE_GIF;
                        break;
                    }
                } else {
                    // Wait until it's time for the next frame, but up to 50ms max at a time to avoid stalling UI thread
                    delay(min((uint32_t)50, frame_delay - time_since_last_frame));
                }

                break;
                }
            case State::SHOW_CREDITS:
                if (right_button) {
                    // Exit credits
                    main_task_.setOtaEnabled(false);
                    state = State::CHOOSE_GIF;
                    tft_.fillScreen(TFT_BLACK);
                    delay(200);
                }
                break;
        }
    }
}

bool DisplayTask::isChristmas() {
    tm local;
    return main_task_.getLocalTime(&local) && local.tm_mon == 11 && local.tm_mday == 25;
}

void DisplayTask::handleLogRendering() {
    uint32_t now = millis();
    // Check for new message
    bool force_redraw = false;
    if (now - last_message_millis_ > 100) {
        std::string* log_string;
        if (xQueueReceive(log_queue_, &log_string, 0) == pdTRUE) {
            last_message_millis_ = now;
            force_redraw = true;
            strncpy(current_message_, log_string->c_str(), sizeof(current_message_));
            delete log_string;
        }
    }

    bool show = show_log_ && (now - last_message_millis_ < 3000);

    if (show && (!message_visible_ || force_redraw)) {
        GifPlayer::set_max_line(124);
        tft_.fillRect(0, 124, DISPLAY_WIDTH, 11, TFT_BLACK);
        tft_.setTextSize(1);
        tft_.setTextDatum(TL_DATUM);
        tft_.drawString(current_message_, 3, 126);
    } else if (!show && message_visible_) {
        tft_.fillRect(0, 124, DISPLAY_WIDTH, 11, TFT_BLACK);
        GifPlayer::set_max_line(-1);
    }
    message_visible_ = show;
}

void DisplayTask::log(const char* msg) {
    Serial.println(msg);
    // Allocate a string for the duration it's in the queue; it is free'd by the queue consumer
    std::string* msg_str = new std::string(msg);

    // Put string in queue (or drop if full to avoid blocking)
    if (xQueueSendToBack(log_queue_, &msg_str, 0) != pdTRUE) {
        delete msg_str;
    }
}

void DisplayTask::log(String msg) {
    log(msg.c_str());
}
