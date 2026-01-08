// main.cpp
#include <Arduino.h>
#include <Wire.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"

#include "measure/measure.h"
#include "control/control.h"
#include "actuation/actuation.h"
#include "ioexpander/ioexpander.h"
#include "statemachine/statemachine.h"
#include "display/display.h"
#include "log/log.h"

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("=== BOOT ===");

    // I2C init (shared bus for MCPs, AD5274, backlight, etc.)
    Wire.begin(21, 19);
    Wire.setClock(400000);

    // System init (mutexes, defaults, curves, initial states)
    system_init();

    // Tasks: core 1 = realtime/control, core 0 = UI/logging

    // 1) Measurements (highest priority, 1 kHz)
    xTaskCreatePinnedToCore(
        measureTask,
        "MEASURE_TASK",
        4096,
        nullptr,
        6,
        nullptr,
        1
    );

    // 2) Control (core logic, 1 kHz)
    xTaskCreatePinnedToCore(
        ControlTask,
        "CONTROL_TASK",
        4096,
        nullptr,
        5,
        nullptr,
        1
    );

    // 3) Actuation (AD5274 + PWM) (1 kHz)
    xTaskCreatePinnedToCore(
        actuationTask,
        "ACT_TASK",
        4096,
        nullptr,
        4,
        nullptr,
        1
    );

    // 4) IO Expander (buttons/LEDs, source enable, fan, encoder events)
    xTaskCreatePinnedToCore(
        ioExpanderTask,
        "IO_TASK",
        4096,
        nullptr,
        3,
        nullptr,
        1
    );

    // 5) State machine (track mode/state, start/pause, latch errors)
    xTaskCreatePinnedToCore(
        statemachineTask,
        "STATE_TASK",
        4096,
        nullptr,
        2,
        nullptr,
        1
    );

    // 6) Display (LVGL) on core 0
    xTaskCreatePinnedToCore(
        displayTask,
        "DISPLAY_TASK",
        8192,
        nullptr,
        1,
        nullptr,
        0
    );

    // 7) Logging (UART + SD) on core 0
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

void loop()
{
    // do nothing; work runs in FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}
