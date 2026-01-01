// display_thread.cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AW9523.h>
#include <lvgl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "display/ili9488_driver.hpp"
#include "display/display.h"
#include "display/ui_screens.hpp"

// ---------------- BACKLIGHT ----------------
static Adafruit_AW9523 aw;
static constexpr uint8_t BL_PINS[] = {0, 1, 2, 3, 4, 5};

static lv_display_t* disp = nullptr;

// ---------------- UI selection ----------------
enum class ActiveUI : uint8_t { UI1 = 0, UI2 = 1, UI3 = 2 };
static ActiveUI current_ui = ActiveUI::UI1;

// ---------------- MODEL ----------------
static DisplayModel g_model;

// ---------------- Input mapping (test) ----------------
// bits 4-8: softkeys 1..5
// bit 9   : encoder press (confirm)
// bit 10  : encoder long press (cancel)
static constexpr uint32_t BIT_SOFTKEY1  = (1u << 4);
static constexpr uint32_t BIT_SOFTKEY2  = (1u << 5);
static constexpr uint32_t BIT_SOFTKEY3  = (1u << 6);
static constexpr uint32_t BIT_SOFTKEY4  = (1u << 7);
static constexpr uint32_t BIT_SOFTKEY5  = (1u << 8);
static constexpr uint32_t BIT_ENC_PRESS = (1u << 9);
static constexpr uint32_t BIT_ENC_LONG  = (1u << 10);

static constexpr uint32_t MASK_UI_KEYS =
    BIT_SOFTKEY1 | BIT_SOFTKEY2 | BIT_SOFTKEY3 | BIT_SOFTKEY4 | BIT_SOFTKEY5 |
    BIT_ENC_PRESS | BIT_ENC_LONG;

// ---------------- Dummy curves (voor test) ----------------
static const int16_t CURVE0[32] = {100,98,96,94,92,90,88,86,84,82,80,78,76,74,72,70,68,66,64,62,60,58,56,54,52,50,48,46,44,42,40,38};
static const int16_t CURVE1[32] = {100,99,98,96,94,92,90,88,85,82,79,76,73,70,67,64,61,58,55,52,49,46,43,40,37,34,31,28,25,22,19,16};
static const int16_t CURVE2[32] = {100,97,95,93,91,89,86,83,80,77,74,71,68,65,62,59,56,53,50,47,44,41,38,35,32,29,26,23,20,17,14,11};

// ---------------- Local UI1 params (totdat system ze krijgt) ----------------
static uint8_t ui1_start_index = 0;
static float   ui1_nominal_v   = 12.0f; // 0..15, step 0.1
static float   ui1_capacity    = 1.0f;  // >=0, step 0.1

// Backups voor cancel (revert)
static uint8_t bk_start_index = 0;
static float   bk_nominal_v   = 12.0f;
static float   bk_capacity    = 1.0f;
static uint8_t bk_curve_id    = 0;

// ---------------- Edit modes ----------------
enum class EditMode : uint8_t {
  VIEW = 0,
  UI1_CURVE,
  UI1_SETPOINT,
  UI1_NOMINAL,
  UI1_CAPACITY,

  UI2_VOLTAGE,
  UI2_ILIMIT,

  UI3_AMPERE,
  UI3_VLIMIT
};

static EditMode edit_mode = EditMode::VIEW;

// ---------------- BACKLIGHT INIT ----------------
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

// ---------------- LVGL DISPLAY PORT ----------------
static void my_flush_cb(lv_display_t* disp_drv, const lv_area_t* area, uint8_t* px_map)
{
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

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

// ---------------- Mapping: SystemSnapshot -> DisplayModel ----------------
static void model_from_system(DisplayModel& m, const SystemSnapshot& s)
{
  const float vout = s.meas.v_out;

  float current = 0.0f;
  if (s.status.mode_current == POWER_MODE_SINK)  current = s.meas.i_sink;
  else                                          current = s.meas.i_source;

  // UI1 curve select
  uint8_t cid = (uint8_t)(s.cfg.curve_id % 3);
  const int16_t* src = (cid == 0) ? CURVE0 : (cid == 1) ? CURVE1 : CURVE2;

  m.ui1.curve_len = 32;
  for (int i = 0; i < 32; ++i) m.ui1.curve[i] = src[i];

  m.ui1.progress_index = (int)ui1_start_index;

  m.ui1.voltage_val = vout;
  m.ui1.current_val = current;
  m.ui1.runtime_sec = (uint32_t)(millis() / 1000);

  m.ui1.capacity_val     = ui1_capacity;
  m.ui1.btn_capacity_val = ui1_capacity;
  m.ui1.nominal_v_val    = ui1_nominal_v;

  m.ui1.state_load = (s.status.mode_current == POWER_MODE_SINK);

  // UI2 / UI3 (voorbereid)
  m.ui2.set_voltage = s.cfg.set_voltage;
  m.ui2.meas_ampere = current;
  m.ui2.vmax = 15.0f;

  m.ui3.set_ampere = s.cfg.set_current;
  m.ui3.meas_voltage = vout;
  m.ui3.imax = 10.0f;
}

// ---------------- Overlay helpers ----------------
static void clear_softkey_highlights()
{
  if (current_ui == ActiveUI::UI1) for (int i=1;i<=5;++i) ui1_set_softkey_highlight(i,false);
  if (current_ui == ActiveUI::UI2) for (int i=1;i<=5;++i) ui2_set_softkey_highlight(i,false);
  if (current_ui == ActiveUI::UI3) for (int i=1;i<=5;++i) ui3_set_softkey_highlight(i,false);
}

static void show_edit_overlay_for_mode(const SystemSnapshot& s)
{
  char value[64];

  if (edit_mode == EditMode::UI1_CURVE) {
    snprintf(value, sizeof(value), "Curve: %u", (unsigned)(s.cfg.curve_id % 3));
    ui_overlay_show("Choose Curve", value, "Rotate = change | Press = OK | Long = Cancel");
    ui1_set_softkey_highlight(1, true);
  }
  else if (edit_mode == EditMode::UI1_SETPOINT) {
    snprintf(value, sizeof(value), "Start index: %u", (unsigned)ui1_start_index);
    ui_overlay_show("Choose Setpoint", value, "Rotate = change | Press = OK | Long = Cancel");
    ui1_set_softkey_highlight(2, true);
  }
  else if (edit_mode == EditMode::UI1_NOMINAL) {
    snprintf(value, sizeof(value), "%.1f V", (double)ui1_nominal_v);
    ui_overlay_show("Nominal voltage", value, "Rotate = change | Press = OK | Long = Cancel");
    ui1_set_softkey_highlight(3, true);
  }
  else if (edit_mode == EditMode::UI1_CAPACITY) {
    snprintf(value, sizeof(value), "%.1f", (double)ui1_capacity);
    ui_overlay_show("Capacity", value, "Rotate = change | Press = OK | Long = Cancel");
    ui1_set_softkey_highlight(4, true);
  }
  else if (edit_mode == EditMode::UI2_VOLTAGE) {
    snprintf(value, sizeof(value), "%.1f V", (double)s.cfg.set_voltage);
    ui_overlay_show("Set Voltage", value, "Rotate = change | Press = OK | Long = Cancel");
    ui2_set_softkey_highlight(1, true);
  }
  else if (edit_mode == EditMode::UI2_ILIMIT) {
    snprintf(value, sizeof(value), "I-limit (TBD)");
    ui_overlay_show("Current limit", value, "Rotate = change | Press = OK | Long = Cancel");
    ui2_set_softkey_highlight(2, true);
  }
  else if (edit_mode == EditMode::UI3_AMPERE) {
    snprintf(value, sizeof(value), "%.1f A", (double)s.cfg.set_current);
    ui_overlay_show("Set Ampere", value, "Rotate = change | Press = OK | Long = Cancel");
    ui3_set_softkey_highlight(1, true);
  }
  else if (edit_mode == EditMode::UI3_VLIMIT) {
    snprintf(value, sizeof(value), "V-limit (TBD)");
    ui_overlay_show("Voltage limit", value, "Rotate = change | Press = OK | Long = Cancel");
    ui3_set_softkey_highlight(2, true);
  }
}

static void update_overlay_value_for_mode(const SystemSnapshot& s)
{
  char value[64];

  if (!ui_overlay_is_visible()) return;

  if (edit_mode == EditMode::UI1_CURVE) {
    snprintf(value, sizeof(value), "Curve: %u", (unsigned)(s.cfg.curve_id % 3));
    ui_overlay_set_value(value);
  }
  else if (edit_mode == EditMode::UI1_SETPOINT) {
    snprintf(value, sizeof(value), "Start index: %u", (unsigned)ui1_start_index);
    ui_overlay_set_value(value);
  }
  else if (edit_mode == EditMode::UI1_NOMINAL) {
    snprintf(value, sizeof(value), "%.1f V", (double)ui1_nominal_v);
    ui_overlay_set_value(value);
  }
  else if (edit_mode == EditMode::UI1_CAPACITY) {
    snprintf(value, sizeof(value), "%.1f", (double)ui1_capacity);
    ui_overlay_set_value(value);
  }
  else if (edit_mode == EditMode::UI2_VOLTAGE) {
    snprintf(value, sizeof(value), "%.1f V", (double)s.cfg.set_voltage);
    ui_overlay_set_value(value);
  }
  else if (edit_mode == EditMode::UI3_AMPERE) {
    snprintf(value, sizeof(value), "%.1f A", (double)s.cfg.set_current);
    ui_overlay_set_value(value);
  }
}

// ---------------- Input handling ----------------
static void enter_edit(EditMode m, const SystemSnapshot& s)
{
  // backup voor cancel
  bk_start_index = ui1_start_index;
  bk_nominal_v   = ui1_nominal_v;
  bk_capacity    = ui1_capacity;
  bk_curve_id    = (uint8_t)(s.cfg.curve_id % 3);

  edit_mode = m;

  clear_softkey_highlights();
  show_edit_overlay_for_mode(s);
}

static void cancel_edit(SystemSnapshot& s)
{
  // revert
  ui1_start_index = bk_start_index;
  ui1_nominal_v   = bk_nominal_v;
  ui1_capacity    = bk_capacity;

  ConfigData cfg = s.cfg;
  cfg.curve_id = bk_curve_id;
  system_write_config(&cfg);

  edit_mode = EditMode::VIEW;
  ui_overlay_hide();
  clear_softkey_highlights();
}

static void confirm_edit()
{
  edit_mode = EditMode::VIEW;
  ui_overlay_hide();
  clear_softkey_highlights();
}

static void apply_encoder_delta(SystemSnapshot& s, int32_t delta)
{
  if (delta == 0) return;

  if (edit_mode == EditMode::UI1_CURVE) {
    int v = (int)(s.cfg.curve_id % 3) + (delta > 0 ? 1 : -1);
    if (v < 0) v = 0;
    if (v > 2) v = 2;
    ConfigData cfg = s.cfg;
    cfg.curve_id = (uint8_t)v;
    system_write_config(&cfg);
  }
  else if (edit_mode == EditMode::UI1_SETPOINT) {
    int v = (int)ui1_start_index + delta;
    if (v < 0) v = 0;
    if (v > 31) v = 31;
    ui1_start_index = (uint8_t)v;
  }
  else if (edit_mode == EditMode::UI1_NOMINAL) {
    float v = ui1_nominal_v + 0.1f * (float)delta;
    if (v < 0.0f) v = 0.0f;
    if (v > 15.0f) v = 15.0f;
    ui1_nominal_v = v;
  }
  else if (edit_mode == EditMode::UI1_CAPACITY) {
    float v = ui1_capacity + 0.1f * (float)delta;
    if (v < 0.0f) v = 0.0f;
    ui1_capacity = v;
  }
  else if (edit_mode == EditMode::UI2_VOLTAGE) {
    float v = s.cfg.set_voltage + 0.1f * (float)delta;
    if (v < 0.0f) v = 0.0f;
    if (v > 15.0f) v = 15.0f;
    ConfigData cfg = s.cfg;
    cfg.set_voltage = v;
    system_write_config(&cfg);
  }
  else if (edit_mode == EditMode::UI3_AMPERE) {
    float v = s.cfg.set_current + 0.1f * (float)delta;
    if (v < 0.0f) v = 0.0f;
    ConfigData cfg = s.cfg;
    cfg.set_current = v;
    system_write_config(&cfg);
  }

  // Overlay live updaten
  SystemSnapshot s2;
  system_read_snapshot(&s2);
  update_overlay_value_for_mode(s2);
}

static void handle_inputs()
{
  SystemSnapshot s;
  system_read_snapshot(&s);

  // Alleen in CONFIG aanpassen (jouw eis)
  if (s.status.state != SYS_STATE_CONFIG) return;

  const uint32_t changed = s.io.buttons_changed_bits;
  const uint32_t raw     = s.io.buttons_raw_bits;

  const bool soft1 = (changed & BIT_SOFTKEY1) && (raw & BIT_SOFTKEY1);
  const bool soft2 = (changed & BIT_SOFTKEY2) && (raw & BIT_SOFTKEY2);
  const bool soft3 = (changed & BIT_SOFTKEY3) && (raw & BIT_SOFTKEY3);
  const bool soft4 = (changed & BIT_SOFTKEY4) && (raw & BIT_SOFTKEY4);
  const bool soft5 = (changed & BIT_SOFTKEY5) && (raw & BIT_SOFTKEY5);

  const bool enc_press = (changed & BIT_ENC_PRESS) && (raw & BIT_ENC_PRESS);
  const bool enc_long  = (changed & BIT_ENC_LONG)  && (raw & BIT_ENC_LONG);

  const int32_t delta = s.io.enc_delta_accum;

  // Cancel / confirm
  if (enc_long && edit_mode != EditMode::VIEW) cancel_edit(s);
  if (enc_press && edit_mode != EditMode::VIEW) confirm_edit();

  // Softkeys (UI1 actief in jouw test)
  if (current_ui == ActiveUI::UI1) {
    if (soft1) enter_edit(EditMode::UI1_CURVE, s);
    if (soft2) enter_edit(EditMode::UI1_SETPOINT, s);
    if (soft3) enter_edit(EditMode::UI1_NOMINAL, s);
    if (soft4) enter_edit(EditMode::UI1_CAPACITY, s);

    if (soft5) {
      // Reset
      ui1_start_index = 0;
      ui1_nominal_v   = 12.0f;
      ui1_capacity    = 1.0f;

      ConfigData cfg = s.cfg;
      cfg.curve_id = 0;
      system_write_config(&cfg);

      edit_mode = EditMode::VIEW;
      ui_overlay_hide();
      clear_softkey_highlights();
    }
  }

  // Encoder delta
  if (edit_mode != EditMode::VIEW) {
    apply_encoder_delta(s, delta);
  }

  // Ack events
  if (changed & MASK_UI_KEYS) system_io_clear_buttons_changed(MASK_UI_KEYS);
  if (delta != 0) system_io_clear_enc_delta();
}

// ---------------- Task ----------------
void displayTask(void* pvParameters)
{
  (void)pvParameters;
  Serial.println("Display task gestart");

  backlight_init_and_on();
  ili9488_init();

  lv_init();
  lvgl_port_init();

  // Start met UI1
  current_ui = ActiveUI::UI1;
  ui1_create();

  const TickType_t period = pdMS_TO_TICKS(100); // rustig
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t last_lv_tick_ms = millis();

  while (true)
  {
    uint32_t now_ms = millis();
    uint32_t dt = now_ms - last_lv_tick_ms;
    last_lv_tick_ms = now_ms;

    lv_tick_inc(dt);
    lv_timer_handler();

    // Inputs (softkeys + encoder) -> overlay + highlights + config writes
    handle_inputs();

    // Update UI data
    SystemSnapshot sys;
    system_read_snapshot(&sys);
    model_from_system(g_model, sys);

    // In jouw test nog altijd UI1
    ui1_update(g_model);

    // yield (WDT vriendelijk)
    vTaskDelay(1);
    vTaskDelayUntil(&lastWake, period);
  }
}
