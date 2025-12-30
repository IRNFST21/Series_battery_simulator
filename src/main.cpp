#include <Arduino.h>
#include <Wire.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "measure/measure.h"

// Task prototypes (zorg dat deze prototypes in hun eigen headers staan)
extern "C" {
  void measureTask(void* pvParameters);
  void ControlTask(void* pvParameters);
  void statemachineTask(void* pvParameters);
  void actuationTask(void* pvParameters);
  void ioExpanderTask(void* pvParameters);
  void displayTask(void* pvParameters);
  void logTask(void* pvParameters);
}

// ================== SETUP ==================
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("start setup");

  system_init();


  xTaskCreatePinnedToCore(
      measureTask,
      "MEASURE_TASK",
      4096,        // stack words
      nullptr,
      5,           // hoogste prio
      nullptr,
      1
    );

  xTaskCreatePinnedToCore(
      ControlTask,
      "CONTROL_TASK",
      4096,        // stack words
      nullptr,
      4,
      nullptr,
      1
    );

  xTaskCreatePinnedToCore(
      statemachineTask,
      "STATEMACHINE_TASK",
      2048,
      nullptr,
      3,
      nullptr,
      0
    );

  // Actuation is I2C / expander / rpot / mode-switch => lager dan Control
  xTaskCreatePinnedToCore(
      actuationTask,
      "ACTUATION_TASK",
      2048,
      nullptr,
      2,
      nullptr,
      0
    );

  xTaskCreatePinnedToCore(
      ioExpanderTask,
      "IO_TASK",
      2048,
      nullptr,
      2,
      nullptr,
      0
    );

  // LVGL/display kost vaak meer stack
  xTaskCreatePinnedToCore(
      displayTask,
      "DISPLAY_TASK",
      8192,
      nullptr,
      1,           // laag, want UI is best-effort
      nullptr,
      0
    );

  // SD logging is ook best-effort
  xTaskCreatePinnedToCore(
      logTask,
      "LOG_TASK",
      4096,
      nullptr,
      1,
      nullptr,
      0
    );

  Serial.println("setup done");
}

// ================== LOOP ==================
void loop()
{
  // Arduino loop niet gebruiken als “supervisor”; je draait FreeRTOS-tasks.
  // Laat hem leeg of met een kleine delay.
  vTaskDelay(pdMS_TO_TICKS(1000));
}