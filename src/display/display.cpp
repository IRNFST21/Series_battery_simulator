// display/display.cpp

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AW9523.h>
#include <lvgl.h>
#include "esp_task_wdt.h"
#include <esp_heap_caps.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"

#include "display/ili9488_driver.hpp"
#include "display/display.h"
#include "display/ui_screens.hpp"

// -----------------------------------------------------------------------------
// Backlight (AW9523)
// -----------------------------------------------------------------------------
static Adafruit_AW9523 aw;
static constexpr uint8_t BL_PINS[] = {0, 1, 2, 3, 4, 5};

static void backlight_init_and_on()
{
    system_lock_i2c();
    const bool ok = aw.begin(0x58);
    system_unlock_i2c();

    if (!ok) {
        Serial.println("AW9523 niet gevonden! (backlight)");
        return;
    }

    Serial.println("AW9523 OK, backlight aan");

    system_lock_i2c();
    for (auto pin : BL_PINS) {
        aw.pinMode(pin, AW9523_LED_MODE);
        aw.analogWrite(pin, 255);
    }
    system_unlock_i2c();
}

// -----------------------------------------------------------------------------
// LVGL display port
// -----------------------------------------------------------------------------
static lv_display_t* g_disp = nullptr;

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

    g_disp = lv_display_create(hor_res, ver_res);
    if (!g_disp) {
        Serial.println("ERROR: lv_display_create failed");
        return;
    }

    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(g_disp, my_flush_cb);

    // RGB565 => 2 bytes/pixel
    static const uint16_t DRAW_BUF_LINES = 10;
    const size_t buf_pixels = (size_t)hor_res * (size_t)DRAW_BUF_LINES;
    const size_t buf_bytes  = buf_pixels * 2;

    // DMA-capable buffers
    uint16_t* buf1 = (uint16_t*)heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint16_t* buf2 = (uint16_t*)heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (!buf1 || !buf2) {
        Serial.println("ERROR: LVGL draw buffer alloc failed");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return;
    }

    lv_display_set_buffers(
        g_disp,
        buf1,
        buf2,
        buf_bytes, // SIZE IN BYTES
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );
}

// -----------------------------------------------------------------------------
// UI routing
// -----------------------------------------------------------------------------
enum class ActiveUI : uint8_t { UI1 = 0, UI2 = 1, UI3 = 2 };
static ActiveUI g_current_ui = ActiveUI::UI1;
static DisplayModel g_model;

// I/O mapping (IOShared.buttons_*):
// - 0..3: mode/start-stop (wordt later door ControlTask gebruikt)
// - 4..8: softkeys (rechts)
// - 10: encoder press (confirm)
// - 11: encoder long press (cancel)
static constexpr uint32_t BTN_SOFT_1    = (1u << 4);
static constexpr uint32_t BTN_SOFT_2    = (1u << 5);
static constexpr uint32_t BTN_SOFT_3    = (1u << 6);
static constexpr uint32_t BTN_SOFT_4    = (1u << 7);
static constexpr uint32_t BTN_SOFT_5    = (1u << 8);
static constexpr uint32_t BTN_ENC_PRESS = (1u << 10);
static constexpr uint32_t BTN_ENC_LONG  = (1u << 11);

static constexpr uint32_t DISPLAY_BTN_MASK =
    BTN_SOFT_1 | BTN_SOFT_2 | BTN_SOFT_3 | BTN_SOFT_4 | BTN_SOFT_5 | BTN_ENC_PRESS | BTN_ENC_LONG;

// Edit context (alleen CONFIG)
typedef enum
{
    EDIT_NONE = 0,
    EDIT_UI1_CURVE,
    EDIT_UI1_START_CAP,
    EDIT_UI1_NOMINAL_V,
    EDIT_UI1_CAPACITY,

    EDIT_UI2_VOLT,
    EDIT_UI2_I_LIMIT,

    EDIT_UI3_CURRENT,
    EDIT_UI3_V_LIMIT,
} EditField;

static EditField g_edit = EDIT_NONE;
static int g_edit_softkey = -1; // 0..4

static UIShared g_edit_backup_ui;

static UiEditField to_ui_edit_field(EditField f)
{
    switch (f) {
        case EDIT_UI1_CURVE:      return UI_EDIT_UI1_CURVE;
        case EDIT_UI1_START_CAP:  return UI_EDIT_UI1_START_CAPACITY;
        case EDIT_UI1_NOMINAL_V:  return UI_EDIT_UI1_NOMINAL_VOLT;
        case EDIT_UI1_CAPACITY:   return UI_EDIT_UI1_CAPACITY;
        case EDIT_UI2_VOLT:       return UI_EDIT_UI2_VOLTAGE;
        case EDIT_UI2_I_LIMIT:    return UI_EDIT_UI2_I_LIMIT;
        case EDIT_UI3_CURRENT:    return UI_EDIT_UI3_CURRENT;
        case EDIT_UI3_V_LIMIT:    return UI_EDIT_UI3_V_LIMIT;
        default:                  return UI_EDIT_NONE;
    }
}

static void clear_all_softkeys()
{
    ui1_softkey_clear_all();
    ui2_softkey_clear_all();
    ui3_softkey_clear_all();
}

static void softkey_set_active_for_current(int idx, bool on)
{
    if (idx < 0 || idx > 4) return;
    switch (g_current_ui) {
        case ActiveUI::UI1: ui1_softkey_set_active(idx, on); break;
        case ActiveUI::UI2: ui2_softkey_set_active(idx, on); break;
        case ActiveUI::UI3: ui3_softkey_set_active(idx, on); break;
    }
}

static void switch_ui_if_needed(UiScreen requested)
{
    ActiveUI desired = g_current_ui;
    if (requested == UI_SCREEN_UI1) desired = ActiveUI::UI1;
    else if (requested == UI_SCREEN_UI2) desired = ActiveUI::UI2;
    else if (requested == UI_SCREEN_UI3) desired = ActiveUI::UI3;

    if (desired == g_current_ui) return;

    g_current_ui = desired;
    clear_all_softkeys();
    ui_overlay_hide();
    g_edit = EDIT_NONE;
    g_edit_softkey = -1;

    switch (g_current_ui) {
        case ActiveUI::UI1: ui1_create(); break;
        case ActiveUI::UI2: ui2_create(); break;
        case ActiveUI::UI3: ui3_create(); break;
    }
}

// -----------------------------------------------------------------------------
// Curve mapping
// -----------------------------------------------------------------------------
static const int16_t* select_curve_ptr(const CurveData& c, uint8_t id)
{
    if (id == 1) return c.curve1;
    if (id == 2) return c.curve2;
    return c.curve0;
}

static void fill_ui1_curve(UI1Model& ui1, const SystemSnapshot& s)
{
    const uint16_t len = (s.curves.len == 0 || s.curves.len > CURVE_LEN) ? CURVE_LEN : s.curves.len;
    ui1.curve_len = (int)len;

    const int16_t* src = select_curve_ptr(s.curves, s.ui.selected_curve_id);
    for (uint16_t i = 0; i < len; ++i) {
        // chart range is 0..100; curves are 0..100
        ui1.curve[i] = src[i];
    }

    // Progress index:
    // - in CONFIG: marker = start_capacity_mAh
    // - in ACTIVE: marker = status.capacity_now_mAh
    const uint32_t cap_total = (s.ui.capacity_set_mAh == 0) ? 1u : s.ui.capacity_set_mAh;
    const uint32_t marker = (s.status.state == SYS_STATE_ACTIVE) ? s.status.capacity_now_mAh : s.ui.start_capacity_mAh;
    uint32_t clamped = marker;
    if (clamped > cap_total) clamped = cap_total;

    const int idx = (int)((uint64_t)clamped * (uint64_t)(len - 1) / (uint64_t)cap_total);
    ui1.progress_index = idx;
}

// -----------------------------------------------------------------------------
// SystemSnapshot -> DisplayModel
// -----------------------------------------------------------------------------
static void model_from_system(DisplayModel& m, const SystemSnapshot& s)
{
    // UI1
    fill_ui1_curve(m.ui1, s);

    // Measurements
    m.ui1.voltage_val = s.meas.v_out;
    m.ui1.current_val = (s.status.mode_current == POWER_MODE_SINK) ? s.meas.i_sink : s.meas.i_source;

    // Runtime label (mm:ss formatting gebeurt in ui_screens)
    m.ui1.runtime_sec = s.status.runtime_sec;

    // Capacity label:
    // - in CONFIG show startpoint
    // - in ACTIVE show current position
    m.ui1.capacity_val = (s.status.state == SYS_STATE_ACTIVE) ? s.status.capacity_now_mAh : s.ui.start_capacity_mAh;

    // Button display values
    m.ui1.nominal_v_val    = s.ui.nominal_voltage_V;
    m.ui1.btn_capacity_val = (float)s.ui.capacity_set_mAh;

    m.ui1.state_load = (s.status.mode_current == POWER_MODE_SINK);

    // UI2
    m.ui2.set_voltage = s.ui.ui2_set_voltage;
    m.ui2.meas_ampere = m.ui1.current_val;
    m.ui2.vmax        = 15.0f;

    // UI3
    m.ui3.set_ampere   = s.ui.ui3_set_current;
    m.ui3.meas_voltage = s.meas.v_out;
    m.ui3.imax         = 10.0f;
}

// -----------------------------------------------------------------------------
// Editing helpers
// -----------------------------------------------------------------------------
static void overlay_for_edit(EditField f, const UIShared& ui)
{
    const char* hint = "Draai: wijzig | Press: OK | Long: Cancel";
    char title[32];
    char value[48];
    title[0] = 0;
    value[0] = 0;

    switch (f) {
        case EDIT_UI1_CURVE:
            snprintf(title, sizeof(title), "Choose Curve");
            snprintf(value, sizeof(value), "Curve: %u", (unsigned)ui.selected_curve_id);
            break;
        case EDIT_UI1_START_CAP:
            snprintf(title, sizeof(title), "Choose Setpoint");
            snprintf(value, sizeof(value), "Start: %lu mAh", (unsigned long)ui.start_capacity_mAh);
            break;
        case EDIT_UI1_NOMINAL_V:
            snprintf(title, sizeof(title), "Nominal voltage");
            snprintf(value, sizeof(value), "%.1f V", (double)ui.nominal_voltage_V);
            break;
        case EDIT_UI1_CAPACITY:
            snprintf(title, sizeof(title), "Capacity");
            snprintf(value, sizeof(value), "%lu mAh", (unsigned long)ui.capacity_set_mAh);
            break;
        case EDIT_UI2_VOLT:
            snprintf(title, sizeof(title), "Voltage");
            snprintf(value, sizeof(value), "%.1f V", (double)ui.ui2_set_voltage);
            break;
        case EDIT_UI2_I_LIMIT:
            snprintf(title, sizeof(title), "Current limit");
            snprintf(value, sizeof(value), "%.1f A", (double)ui.ui2_current_limit);
            break;
        case EDIT_UI3_CURRENT:
            snprintf(title, sizeof(title), "Ampere");
            snprintf(value, sizeof(value), "%.1f A", (double)ui.ui3_set_current);
            break;
        case EDIT_UI3_V_LIMIT:
            snprintf(title, sizeof(title), "Voltage limit");
            snprintf(value, sizeof(value), "%.1f V", (double)ui.ui3_voltage_limit);
            break;
        default:
            snprintf(title, sizeof(title), "Edit");
            snprintf(value, sizeof(value), "");
            break;
    }

    ui_overlay_show(title, value, hint);
}

static void begin_edit(EditField f, int softkey_idx, const SystemSnapshot& s)
{
    g_edit = f;
    g_edit_softkey = softkey_idx;
    g_edit_backup_ui = s.ui;

    clear_all_softkeys();
    softkey_set_active_for_current(softkey_idx, true);

    overlay_for_edit(f, s.ui);

    // Event voor ControlTask (later)
    UIEvents ev = s.ui_events;
    ev.flags |= UI_EVT_EDIT_STARTED;
    ev.field = to_ui_edit_field(f);
    ev.seq++;
    system_write_ui_events(&ev);
}

static void cancel_edit(const SystemSnapshot& s)
{
    if (g_edit == EDIT_NONE) return;

    // Restore UIShared
    system_write_ui_shared(&g_edit_backup_ui);

    UIEvents ev = s.ui_events;
    ev.flags |= UI_EVT_EDIT_CANCELLED;
    ev.field = to_ui_edit_field(g_edit);
    ev.seq++;
    system_write_ui_events(&ev);

    ui_overlay_hide();
    clear_all_softkeys();
    g_edit = EDIT_NONE;
    g_edit_softkey = -1;
}

static void confirm_edit(const SystemSnapshot& s)
{
    if (g_edit == EDIT_NONE) return;

    UIEvents ev = s.ui_events;
    ev.flags |= UI_EVT_EDIT_CONFIRMED | UI_EVT_PARAM_CHANGED;
    ev.field = to_ui_edit_field(g_edit);
    ev.seq++;
    system_write_ui_events(&ev);

    ui_overlay_hide();
    clear_all_softkeys();
    g_edit = EDIT_NONE;
    g_edit_softkey = -1;
}

static float step_01(float v, int delta, float vmin, float vmax)
{
    float out = v + 0.1f * (float)delta;
    if (out < vmin) out = vmin;
    if (out > vmax) out = vmax;
    out = (float)((int)(out * 10.0f + 0.5f)) / 10.0f; // snap 0.1
    return out;
}

static void apply_encoder_delta_to_ui(EditField f, int delta, UIShared* ui)
{
    if (!ui || delta == 0) return;

    switch (f) {
        case EDIT_UI1_CURVE: {
            int id = (int)ui->selected_curve_id + delta;
            if (id < 0) id = 0;
            if (id > 2) id = 2;
            ui->selected_curve_id = (uint8_t)id;
        } break;

        case EDIT_UI1_START_CAP: {
            int64_t v = (int64_t)ui->start_capacity_mAh + (int64_t)delta * 10; // 10 mAh steps
            if (v < 0) v = 0;
            if ((uint64_t)v > ui->capacity_set_mAh) v = ui->capacity_set_mAh;
            ui->start_capacity_mAh = (uint32_t)v;
        } break;

        case EDIT_UI1_NOMINAL_V:
            ui->nominal_voltage_V = step_01(ui->nominal_voltage_V, delta, 0.0f, 15.0f);
            break;

        case EDIT_UI1_CAPACITY: {
            int64_t v = (int64_t)ui->capacity_set_mAh + (int64_t)delta * 50; // 50 mAh steps
            if (v < 0) v = 0;
            if (v > 200000) v = 200000;
            ui->capacity_set_mAh = (uint32_t)v;
            if (ui->start_capacity_mAh > ui->capacity_set_mAh) ui->start_capacity_mAh = ui->capacity_set_mAh;
        } break;

        case EDIT_UI2_VOLT:
            ui->ui2_set_voltage = step_01(ui->ui2_set_voltage, delta, 0.0f, 15.0f);
            break;

        case EDIT_UI2_I_LIMIT:
            ui->ui2_current_limit = step_01(ui->ui2_current_limit, delta, 0.0f, 50.0f);
            break;

        case EDIT_UI3_CURRENT:
            ui->ui3_set_current = step_01(ui->ui3_set_current, delta, 0.0f, 50.0f);
            break;

        case EDIT_UI3_V_LIMIT:
            ui->ui3_voltage_limit = step_01(ui->ui3_voltage_limit, delta, 0.0f, 15.0f);
            break;

        default:
            break;
    }
}

static void handle_inputs(const SystemSnapshot& s)
{
    // Alleen in CONFIG mag display setpoints schrijven.
    if (s.status.state != SYS_STATE_CONFIG) {
        if (g_edit != EDIT_NONE) {
            ui_overlay_hide();
            clear_all_softkeys();
            g_edit = EDIT_NONE;
            g_edit_softkey = -1;
        }
        return;
    }

    const uint32_t changed = s.io.buttons_changed_bits & DISPLAY_BTN_MASK;
    const uint32_t raw     = s.io.buttons_raw_bits;
    const int enc_delta = s.io.enc_delta_accum;

    // Start edit via softkeys
    if (g_edit == EDIT_NONE) {
        if ((changed & BTN_SOFT_1) && (raw & BTN_SOFT_1)) {
            if (g_current_ui == ActiveUI::UI1) begin_edit(EDIT_UI1_CURVE, 0, s);
            else if (g_current_ui == ActiveUI::UI2) begin_edit(EDIT_UI2_VOLT, 0, s);
            else begin_edit(EDIT_UI3_CURRENT, 0, s);
        } else if ((changed & BTN_SOFT_2) && (raw & BTN_SOFT_2)) {
            if (g_current_ui == ActiveUI::UI1) begin_edit(EDIT_UI1_START_CAP, 1, s);
            else if (g_current_ui == ActiveUI::UI2) begin_edit(EDIT_UI2_I_LIMIT, 1, s);
            else begin_edit(EDIT_UI3_V_LIMIT, 1, s);
        } else if ((changed & BTN_SOFT_3) && (raw & BTN_SOFT_3)) {
            if (g_current_ui == ActiveUI::UI1) begin_edit(EDIT_UI1_NOMINAL_V, 2, s);
        } else if ((changed & BTN_SOFT_4) && (raw & BTN_SOFT_4)) {
            if (g_current_ui == ActiveUI::UI1) begin_edit(EDIT_UI1_CAPACITY, 3, s);
        } else if ((changed & BTN_SOFT_5) && (raw & BTN_SOFT_5)) {
            UIEvents ev = s.ui_events;
            ev.flags |= UI_EVT_RESET_REQUESTED;
            ev.field = UI_EDIT_NONE;
            ev.seq++;
            system_write_ui_events(&ev);
            ui_overlay_show("Reset", "requested", "");
        }
    }

    // In edit: encoder + confirm/cancel
    if (g_edit != EDIT_NONE) {
        if (enc_delta != 0) {
            UIShared ui = s.ui;
            apply_encoder_delta_to_ui(g_edit, enc_delta, &ui);
            system_write_ui_shared(&ui);

            UIEvents ev = s.ui_events;
            ev.flags |= UI_EVT_PARAM_CHANGED;
            ev.field = to_ui_edit_field(g_edit);
            ev.seq++;
            system_write_ui_events(&ev);

            overlay_for_edit(g_edit, ui);
        }

        if ((changed & BTN_ENC_PRESS) && (raw & BTN_ENC_PRESS)) {
            confirm_edit(s);
        }
        if ((changed & BTN_ENC_LONG) && (raw & BTN_ENC_LONG)) {
            cancel_edit(s);
        }
    }

    if (changed) system_io_clear_buttons_changed(changed);
    if (enc_delta != 0) system_io_clear_enc_delta();
}

// -----------------------------------------------------------------------------
// Task
// -----------------------------------------------------------------------------
extern "C" void displayTask(void* pvParameters)
{
    (void)pvParameters;

    Serial.println("Display task gestart");

    backlight_init_and_on();
    ili9488_init();

    Serial.println("LVGL init start");
    lv_init();
    Serial.println("LVGL port init");
    lvgl_port_init();

    // Start UI1
    g_current_ui = ActiveUI::UI1;
    ui1_create();

    const TickType_t period = pdMS_TO_TICKS(50); // 20 Hz
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t last_lv_tick_ms = millis();

    while (true)
    {
        const uint32_t now_ms = millis();
        const uint32_t dt = now_ms - last_lv_tick_ms;
        last_lv_tick_ms = now_ms;
        lv_tick_inc(dt);
        lv_timer_handler();

        SystemSnapshot sys;
        system_read_snapshot(&sys);

        switch_ui_if_needed(sys.ui.active_screen);
        handle_inputs(sys);

        model_from_system(g_model, sys);

        switch (g_current_ui) {
            case ActiveUI::UI1: ui1_update(g_model); break;
            case ActiveUI::UI2: ui2_update(g_model); break;
            case ActiveUI::UI3: ui3_update(g_model); break;
        }

        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, period);
    }
}
