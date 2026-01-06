// main.cpp

#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "measure/measure.h"
#include "display/display.h"

// ----------------------------
// SIMULATIE (tijdelijk: totdat we IOexpanderTask + ControlTask hebben)
// - Laat zien dat displayTask UIShared/Status/IO... in system gebruikt.
// ----------------------------

static void simulateUiTask(void* pv)
{
    (void)pv;
    Serial.println("simulateUiTask started");

    // Zet init state
    SystemStatus st{};
    st.state = SYS_STATE_CONFIG;
    st.mode_current = POWER_MODE_EMULATE;
    st.mode_pending = POWER_MODE_EMULATE;
    st.runtime_sec = 0;
    st.capacity_now_mAh = 0;
    system_write_status(&st);

    UIShared ui{};
    ui.active_screen = UI_SCREEN_UI1;
    ui.selected_curve_id = 0;
    ui.nominal_voltage_V = 12.0f;
    ui.capacity_set_mAh = 2000;
    ui.start_capacity_mAh = 0;
    ui.ui2_set_voltage = 5.0f;
    ui.ui2_current_limit = 2.0f;
    ui.ui3_set_current = 1.0f;
    ui.ui3_voltage_limit = 12.0f;
    system_write_ui_shared(&ui);

    // Eenvoudige demo: elke paar sec een softkey press + encoder delta.
    uint32_t phase = 0;
    for (;;)
    {
        SystemSnapshot s;
        system_read_snapshot(&s);
        IOShared io = s.io;

        io.buttons_changed_bits = 0;
        io.enc_delta_accum = 0;

        // 0) Soft1 -> Choose curve, draai +1, confirm
        // 1) Soft2 -> Choose setpoint, draai +50 steps, confirm
        // 2) Soft3 -> Nominal voltage, draai -5 (0.5V), confirm
        // 3) Soft4 -> Capacity, draai +10 (500mAh), confirm
        // 4) Switch naar RUN en laat progress lopen
        // 5) Terug naar CONFIG

        if (phase == 0) {
            io.buttons_raw_bits |= (1u << 4);
            io.buttons_changed_bits |= (1u << 4);
        } else if (phase == 1) {
            io.enc_delta_accum = +1;
        } else if (phase == 2) {
            io.buttons_raw_bits |= (1u << 10);
            io.buttons_changed_bits |= (1u << 10);
        } else if (phase == 3) {
            io.buttons_raw_bits |= (1u << 5);
            io.buttons_changed_bits |= (1u << 5);
        } else if (phase == 4) {
            io.enc_delta_accum = +50;
        } else if (phase == 5) {
            io.buttons_raw_bits |= (1u << 10);
            io.buttons_changed_bits |= (1u << 10);
        } else if (phase == 6) {
            io.buttons_raw_bits |= (1u << 6);
            io.buttons_changed_bits |= (1u << 6);
        } else if (phase == 7) {
            io.enc_delta_accum = -5;
        } else if (phase == 8) {
            io.buttons_raw_bits |= (1u << 10);
            io.buttons_changed_bits |= (1u << 10);
        } else if (phase == 9) {
            io.buttons_raw_bits |= (1u << 7);
            io.buttons_changed_bits |= (1u << 7);
        } else if (phase == 10) {
            io.enc_delta_accum = +10;
        } else if (phase == 11) {
            io.buttons_raw_bits |= (1u << 10);
            io.buttons_changed_bits |= (1u << 10);
        } else if (phase == 12) {
            // RUN: zet status ACTIVE en laat capacity_now oplopen
            SystemSnapshot ss;
            system_read_snapshot(&ss);
            SystemStatus st2 = ss.status;
            st2.state = SYS_STATE_ACTIVE;
            st2.runtime_sec = 0;
            st2.capacity_now_mAh = ss.ui.start_capacity_mAh;
            system_write_status(&st2);

            // Run 10 seconden
            for (uint32_t t = 0; t < 10; ++t) {
                system_read_snapshot(&ss);
                st2 = ss.status;
                st2.runtime_sec = t;
                uint32_t step = ss.ui.capacity_set_mAh / 20;
                st2.capacity_now_mAh = ss.ui.start_capacity_mAh + t * step;
                if (st2.capacity_now_mAh > ss.ui.capacity_set_mAh) st2.capacity_now_mAh = ss.ui.capacity_set_mAh;
                system_write_status(&st2);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            system_read_snapshot(&ss);
            st2 = ss.status;
            st2.state = SYS_STATE_CONFIG;
            system_write_status(&st2);
        }

        system_write_io_shared(&io);

        phase++;
        if (phase > 13) phase = 0;
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("=== BOOT ===");

    // I2C init (1x)
    Wire.begin(21, 19);
    Wire.setClock(100000);

    // System init (mutexen + defaults)
    system_init();

    // Tasks
    xTaskCreatePinnedToCore(measureTask,   "MEASURE_TASK",  4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(displayTask,   "DISPLAY_TASK",  8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(simulateUiTask,"SIM_UI_TASK",   4096, nullptr, 1, nullptr, 1);

    Serial.println("setup done");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
