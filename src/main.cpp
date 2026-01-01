#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "display/display.h"

// --- Zelfde bitmapping als display.cpp ---
static constexpr uint32_t SOFT1 = (1u << 4);
static constexpr uint32_t SOFT2 = (1u << 5);
static constexpr uint32_t SOFT3 = (1u << 6);
static constexpr uint32_t SOFT4 = (1u << 7);
static constexpr uint32_t SOFT5 = (1u << 8);
static constexpr uint32_t ENCP  = (1u << 9);
static constexpr uint32_t ENCL  = (1u << 10);

static void sim_button_press(uint32_t bit)
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  IOShared io = s.io;
  io.buttons_raw_bits |= bit;
  io.buttons_changed_bits |= bit;
  system_write_io_shared(&io);
}

static void sim_button_release(uint32_t bit)
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  IOShared io = s.io;
  io.buttons_raw_bits &= ~bit;
  io.buttons_changed_bits |= bit;
  system_write_io_shared(&io);
}

static void sim_encoder_delta(int32_t delta)
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  IOShared io = s.io;
  io.enc_delta_accum += delta;
  system_write_io_shared(&io);
}

static void simulateInputTask(void*)
{
  Serial.println("SIM_INPUT_TASK started");

  // Zorg dat we in CONFIG staan (anders weigert display edits)
  {
    SystemSnapshot s;
    system_read_snapshot(&s);
    SystemStatus st = s.status;
    st.state = SYS_STATE_CONFIG;
    system_write_status(&st);
  }

  // Demo flow UI1
  while (true)
  {
    // Choose Curve
    sim_button_press(SOFT1); vTaskDelay(pdMS_TO_TICKS(120)); sim_button_release(SOFT1);
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_encoder_delta(+1);
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_button_press(ENCP); vTaskDelay(pdMS_TO_TICKS(80)); sim_button_release(ENCP);

    vTaskDelay(pdMS_TO_TICKS(2500));

    // Nominal voltage
    sim_button_press(SOFT3); vTaskDelay(pdMS_TO_TICKS(120)); sim_button_release(SOFT3);
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_encoder_delta(+5); // +0.5V
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_button_press(ENCP); vTaskDelay(pdMS_TO_TICKS(80)); sim_button_release(ENCP);

    vTaskDelay(pdMS_TO_TICKS(2500));

    // Capacity -> cancel
    sim_button_press(SOFT4); vTaskDelay(pdMS_TO_TICKS(120)); sim_button_release(SOFT4);
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_encoder_delta(+10);
    vTaskDelay(pdMS_TO_TICKS(300));
    sim_button_press(ENCL); vTaskDelay(pdMS_TO_TICKS(80)); sim_button_release(ENCL);

    vTaskDelay(pdMS_TO_TICKS(2500));

    // Reset
    sim_button_press(SOFT5); vTaskDelay(pdMS_TO_TICKS(120)); sim_button_release(SOFT5);

    vTaskDelay(pdMS_TO_TICKS(4000));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== BOOT ===");

  Wire.begin(21, 19);
  Wire.setClock(400000);

  system_init();

  // Display core 1 (stabieler)
  xTaskCreatePinnedToCore(displayTask, "DISPLAY_TASK", 8192, nullptr, 1, nullptr, 1);

  // Sim input core 0
  xTaskCreatePinnedToCore(simulateInputTask, "SIM_INPUT_TASK", 4096, nullptr, 1, nullptr, 0);

  Serial.println("setup done");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}




//////////////////////////////////////////////////////////////////////////////////////////////////////

// // main.cpp
// #include <Arduino.h>
// #include <Wire.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "system/system.h"
// #include "measure/measure.h"
// #include "display/display.h"


// // Task prototypes
// //void measureTask(void* pvParameters);
// //void ControlTask(void* pvParameters);
// //void statemachineTask(void* pvParameters);
// //void actuationTask(void* pvParameters);
// //void ioExpan  derTask(void* pvParameters);
// void displayTask(void* pvParameters);
// //void logTask(void* pvParameters);

// void setup()
// {
//     Serial.begin(115200);
//     delay(200);
//     Serial.println("start setup");

//     // I2C init 1x in setup
//     Wire.begin(21, 19);
//     Wire.setClock(400000);

//     // System init (mutexen + defaults + curves)
//     system_init();

//     // Tasks
//     //xTaskCreatePinnedToCore(measureTask,      "MEASURE_TASK",      4096, nullptr, 5, nullptr, 1);
//     //xTaskCreatePinnedToCore(ControlTask,      "CONTROL_TASK",      4096, nullptr, 4, nullptr, 1);

//     //xTaskCreatePinnedToCore(statemachineTask, "STATEMACHINE_TASK", 2048, nullptr, 3, nullptr, 0);
//     //xTaskCreatePinnedToCore(actuationTask,    "ACTUATION_TASK",    2048, nullptr, 2, nullptr, 0);
//     //xTaskCreatePinnedToCore(ioExpanderTask,   "IO_TASK",           2048, nullptr, 2, nullptr, 0);

//     xTaskCreatePinnedToCore(displayTask,      "DISPLAY_TASK",      8192, nullptr, 1, nullptr, 0);
//     //xTaskCreatePinnedToCore(logTask,          "LOG_TASK",          4096, nullptr, 1, nullptr, 0);

//     Serial.println("setup done");
// }

// void loop()
// {
//     // Niets doen; FreeRTOS tasks draaien
//     vTaskDelay(pdMS_TO_TICKS(1000));
// }
