// main.cpp
#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "measure/measure.h"
#include "display/display.h"


// Task prototypes
//void measureTask(void* pvParameters);
//void ControlTask(void* pvParameters);
//void statemachineTask(void* pvParameters);
//void actuationTask(void* pvParameters);
//void ioExpan  derTask(void* pvParameters);
void displayTask(void* pvParameters);
//void logTask(void* pvParameters);

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("start setup");

    // I2C init 1x in setup
    Wire.begin(21, 19);
    Wire.setClock(400000);

    // System init (mutexen + defaults + curves)
    system_init();

    // Tasks
    //xTaskCreatePinnedToCore(measureTask,      "MEASURE_TASK",      4096, nullptr, 5, nullptr, 1);
    //xTaskCreatePinnedToCore(ControlTask,      "CONTROL_TASK",      4096, nullptr, 4, nullptr, 1);

    //xTaskCreatePinnedToCore(statemachineTask, "STATEMACHINE_TASK", 2048, nullptr, 3, nullptr, 0);
    //xTaskCreatePinnedToCore(actuationTask,    "ACTUATION_TASK",    2048, nullptr, 2, nullptr, 0);
    //xTaskCreatePinnedToCore(ioExpanderTask,   "IO_TASK",           2048, nullptr, 2, nullptr, 0);

    xTaskCreatePinnedToCore(displayTask,      "DISPLAY_TASK",      8192, nullptr, 1, nullptr, 0);
    //xTaskCreatePinnedToCore(logTask,          "LOG_TASK",          4096, nullptr, 1, nullptr, 0);

    Serial.println("setup done");
}

void loop()
{
    // Niets doen; FreeRTOS tasks draaien
    vTaskDelay(pdMS_TO_TICKS(1000));
}
