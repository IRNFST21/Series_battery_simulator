// main.cpp (display-only test)
#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "display/display.h"

// Zelfde mapping als in display.cpp
static constexpr uint32_t BTN_SOFT_1    = (1u << 4);
static constexpr uint32_t BTN_SOFT_2    = (1u << 5);
static constexpr uint32_t BTN_SOFT_3    = (1u << 6);
static constexpr uint32_t BTN_SOFT_4    = (1u << 7);
static constexpr uint32_t BTN_SOFT_5    = (1u << 8);
static constexpr uint32_t BTN_ENC_PRESS = (1u << 10);
static constexpr uint32_t BTN_ENC_LONG  = (1u << 11);

static void simulate_set_raw(uint32_t mask, bool down)
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  IOShared io = s.io;
  const bool was = (io.buttons_raw_bits & mask) != 0;

  if (down) io.buttons_raw_bits |= mask; else io.buttons_raw_bits &= ~mask;

  // Changed bit alleen zetten als er daadwerkelijk een flank is
  const bool now = down;
  if (was != now) io.buttons_changed_bits |= mask;

  system_write_io_shared(&io);
}

static void simulate_press(uint32_t mask, uint32_t hold_ms = 50)
{
  simulate_set_raw(mask, true);
  vTaskDelay(pdMS_TO_TICKS(hold_ms));
  simulate_set_raw(mask, false);
}

static void simulate_encoder_delta(int32_t steps)
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  IOShared io = s.io;
  io.enc_delta_accum += steps;
  system_write_io_shared(&io);
}

static void simulateUiTask(void* pv)
{
  (void)pv;
  Serial.println("simulateUiTask started");

  // Zorg dat we in CONFIG blijven en UI1 tonen
  {
    SystemSnapshot s;
    system_read_snapshot(&s);

    SystemStatus st = s.status;
    st.state = SYS_STATE_CONFIG;
    st.mode_current = POWER_MODE_EMULATE;
    st.mode_pending = POWER_MODE_EMULATE;
    system_write_status(&st);

    UIShared ui = s.ui;
    ui.active_screen = UI_SCREEN_EMULATE;
    system_write_ui_shared(&ui);
  }

  vTaskDelay(pdMS_TO_TICKS(2000));

  // 1) Choose Curve -> draai -> confirm
  simulate_press(BTN_SOFT_1);
  vTaskDelay(pdMS_TO_TICKS(800));
  simulate_encoder_delta(+1);
  vTaskDelay(pdMS_TO_TICKS(400));
  simulate_encoder_delta(+1);
  vTaskDelay(pdMS_TO_TICKS(400));
  simulate_encoder_delta(+1);
  vTaskDelay(pdMS_TO_TICKS(700));
  simulate_press(BTN_ENC_PRESS);

  vTaskDelay(pdMS_TO_TICKS(1500));

  // 2) Nominal voltage -> draai -> confirm
  simulate_press(BTN_SOFT_3);
  vTaskDelay(pdMS_TO_TICKS(700));
  simulate_encoder_delta(+50); // +5.0 V
  vTaskDelay(pdMS_TO_TICKS(700));
  simulate_press(BTN_ENC_PRESS);

  vTaskDelay(pdMS_TO_TICKS(1500));

  // 3) Capacity -> draai -> cancel
  simulate_press(BTN_SOFT_4);
  vTaskDelay(pdMS_TO_TICKS(700));
  simulate_encoder_delta(+30); // +3.0 F
  vTaskDelay(pdMS_TO_TICKS(700));
  simulate_press(BTN_ENC_LONG, 600);

  vTaskDelay(pdMS_TO_TICKS(1500));

  // 4) Reset
  simulate_press(BTN_SOFT_5);

  // Herhaal langzaam
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Wissel curve nog eens
    simulate_press(BTN_SOFT_1);
    vTaskDelay(pdMS_TO_TICKS(700));
    simulate_encoder_delta(+1);
    vTaskDelay(pdMS_TO_TICKS(700));
    simulate_press(BTN_ENC_PRESS);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("=== BOOT ===");

  // I2C init (1x)
  Wire.begin(21, 19);
  Wire.setClock(400000);

  system_init();

  xTaskCreatePinnedToCore(
      displayTask,
      "DISPLAY_TASK",
      8192,
      nullptr,
      1,
      nullptr,
      0);

  xTaskCreatePinnedToCore(
      simulateUiTask,
      "SIM_UI_TASK",
      4096,
      nullptr,
      1,
      nullptr,
      1);

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
