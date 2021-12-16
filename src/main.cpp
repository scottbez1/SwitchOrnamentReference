#include <Arduino.h>

#include "display_task.h"
#include "main_task.h"

MainTask main_task = MainTask(0);
DisplayTask display_task = DisplayTask(main_task, 1);

void setup() {
  Serial.begin(921600);

  main_task.begin();
  display_task.begin();

  vTaskDelete(NULL);
}


void loop() {
  assert(false);
}