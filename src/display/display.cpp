// display.cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AW9523.h>
#include <lvgl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"

// Let op: jouw ui_screens.cpp include't "display/ui_screens.hpp"
// Houd dus dit pad consistent met jouw projectstructuur.
#include "display/ui_screens.hpp"
#include "display/ili9488_driver.hpp"

// ---------------- BACKLIGHT ----------------
static Adafruit_AW9523 aw;
static constexpr uint8_t BL_PINS[] = {0, 1, 2, 3, 4, 5};

static lv_display_t* disp = nullptr;

// Track UI op basis van state
static SystemState last_state = (SystemState)255;

// DisplayModel dat jouw ui_screens gebruikt
static DisplayModel g_model;

// ---- helpers ----
static inline void backlight_init_and_on()
{
    system_lock_i2c();
    const bool ok = aw.begin(0x58);
    system_unlock_i2c();

    if (!ok)
    {
        Serial.println("AW9523 niet gevonden (backlight)");
        return;
    }

    system_lock_i2c();
    for (auto pin : BL_PINS)
    {
        aw.pinMode(pin, AW9523_LED_MODE);
        aw.analogWrite(pin, 255);
    }
    system_unlock_i2c();
}

static void my_flush_cb(lv_display_t* disp_drv, const lv_area_t* area, uint8_t* px_map)
{
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    ili9488_push_pixels(area->x1, area->y1, w, h, (const uint8_t*)px_map);
    lv_display_flush_ready(disp_drv);
}

static void lvgl_port_init()
{
    const uint16_t hor_res = 480;
    const uint16_t ver_res = 320;

    disp = lv_display_create(hor_res, ver_res);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);

    static const uint16_t DRAW_BUF_LINES = 10;
    static lv_color_t buf1[480 * DRAW_BUF_LINES];
    static lv_color_t buf2[480 * DRAW_BUF_LINES];
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

static void select_curve_into_model(const SystemSnapshot& s, DisplayModel& m)
{
    // ui1 gebruikt altijd curve[32]
    m.ui1.curve_len = 32;

    const int16_t* src = s.curves.curve0;
    if (s.cfg.curve_id == 1) src = s.curves.curve1;
    else if (s.cfg.curve_id == 2) src = s.curves.curve2;

    for (int i = 0; i < 32; ++i) m.ui1.curve[i] = src[i];
}

static void map_system_to_model(const SystemSnapshot& s, DisplayModel& m)
{
    // Metingen
    const float vout = s.meas.v_out;
    const float current = (s.status.mode_current == POWER_MODE_SINK) ? s.meas.i_sink : s.meas.i_source;

    // UI1 (Emulate)
    m.ui1.voltage_val    = vout;
    m.ui1.current_val    = current;
    m.ui1.capacity_val   = 0.0f;                 // nog geen echte capaciteit -> later toevoegen
    m.ui1.runtime_sec    = (uint32_t)(millis() / 1000);
    m.ui1.state_load     = (s.status.mode_current == POWER_MODE_SINK); // interpretatie: sink = load
    m.ui1.nominal_v_val  = s.cfg.set_voltage;    // voorbeeld mapping
    m.ui1.btn_capacity_val = 0.0f;
    // progress_index: placeholder. Later koppel je dit aan echte curve-voortgang.
    if (m.ui1.progress_index < 0) m.ui1.progress_index = 0;
    if (m.ui1.progress_index > 31) m.ui1.progress_index = 31;

    select_curve_into_model(s, m);

    // UI2 (set voltage)
    m.ui2.set_voltage  = s.cfg.set_voltage;
    m.ui2.meas_ampere  = current;
    if (m.ui2.vmax <= 0.1f) m.ui2.vmax = 20.0f;

    // UI3 (set current)
    m.ui3.set_ampere   = s.cfg.set_current;
    m.ui3.meas_voltage = vout;
    if (m.ui3.imax <= 0.1f) m.ui3.imax = 5.0f;
}

static void load_ui_for_state(SystemState st)
{
    // Jij definieert: welke state toont welke UI?
    // Hier een simpele mapping:
    // CONFIG/READY -> UI1
    // ACTIVE       -> UI2
    // ERROR        -> UI3

    if (st == SYS_STATE_ACTIVE)
        ui2_create();
    else if (st == SYS_STATE_ERROR)
        ui3_create();
    else
        ui1_create();
}

void displayTask(void* pvParameters)
{
    (void)pvParameters;
    Serial.println("Display task gestart");

    // Zorg dat Wire.begin() al in setup() gedaan is (main.cpp)
    backlight_init_and_on();

    ili9488_init();

    lv_init();
    lvgl_port_init();

    // Init model met veilige defaults
    memset(&g_model, 0, sizeof(g_model));
    g_model.ui1.curve_len = 32;
    g_model.ui2.vmax = 20.0f;
    g_model.ui3.imax = 5.0f;

    // Start UI op basis van huidige state
    SystemSnapshot sys;
    system_read_snapshot(&sys);
    load_ui_for_state(sys.status.state);
    last_state = sys.status.state;

    const TickType_t period = pdMS_TO_TICKS(50); // 20 Hz
    TickType_t lastWake = xTaskGetTickCount();

    uint32_t last_lv_tick_ms = millis();

    while (true)
    {
        // LVGL tick/handler
        uint32_t now_ms = millis();
        uint32_t dt = now_ms - last_lv_tick_ms;
        last_lv_tick_ms = now_ms;

        lv_tick_inc(dt);
        lv_timer_handler();

        // Read system data
        system_read_snapshot(&sys);

        // Switch UI if state changed
        if (sys.status.state != last_state)
        {
            load_ui_for_state(sys.status.state);
            last_state = sys.status.state;
        }

        // Map system -> model, update screen
        map_system_to_model(sys, g_model);

        // Update juiste UI afhankelijk van state (zelfde mapping als load_ui_for_state)
        if (sys.status.state == SYS_STATE_CONFIG)
            ui2_update(g_model);
        else if (sys.status.state == SYS_STATE_ERROR)
            ui3_update(g_model);
        else
            ui1_update(g_model);

        vTaskDelayUntil(&lastWake, period);
    }
}
