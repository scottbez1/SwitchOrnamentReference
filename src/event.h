#pragma once

#include <stdint.h>

#define BUTTON_ID_LEFT 0
#define BUTTON_ID_RIGHT 1

enum class EventType {
    BUTTON,
};

struct EventButton {
    uint8_t button_id;
    uint8_t event;
};

struct Event {
    EventType type;
    union {
        EventButton button;
    };
};
