#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


void ControlTask(void *pvParameters)
{
    

    for (;;)
    {


        vTaskDelay(pdMS_TO_TICKS(10)); // sampletijd state machine
    }
}