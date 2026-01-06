// main.cpp
#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"

#include "measure/measure.h"
#include "display/display.h"
#include "ioexpander/ioexpander.h"
#include "statemachine/statemachine.h"

void setup()
{
    Serial.begin(115200);
    delay(200);

    Serial.println("=== BOOT ===");

    // I2C init 1x
    Wire.begin(21, 19);
    Wire.setClock(400000);

    // System init (mutexen + defaults + curves)
    system_init();

    // --- Tasks ---
    // Tip: snelle IO/measure op core 1, display op core 0.

    xTaskCreatePinnedToCore(measureTask, "MEASURE_TASK",
                            4096, nullptr, 5, nullptr, 1);

    xTaskCreatePinnedToCore(ioExpanderTask, "IO_TASK",
                            4096, nullptr, 4, nullptr, 1);

    xTaskCreatePinnedToCore(statemachineTask, "STATE_TASK",
                            4096, nullptr, 3, nullptr, 1);

    xTaskCreatePinnedToCore(displayTask, "DISPLAY_TASK",
                            8192, nullptr, 1, nullptr, 0);

    Serial.println("setup done");
}

void loop()
{
    // Niets doen; FreeRTOS tasks draaien
    vTaskDelay(pdMS_TO_TICKS(1000));
}
