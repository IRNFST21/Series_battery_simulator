// display.cpp
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

// ---------------- BACKLIGHT ----------------
static Adafruit_AW9523 aw;
static constexpr uint8_t BL_PINS[] = {0, 1, 2, 3, 4, 5};

static lv_display_t* disp = nullptr;

// ---------------- UI selection ----------------
enum class ActiveUI : uint8_t { UI1 = 0, UI2 = 1, UI3 = 2 };
static ActiveUI current_ui = ActiveUI::UI1;

// ---------------- MODEL ----------------
static DisplayModel g_model;

// ---------------- INPUT bit mapping (IOShared.buttons_*) ----------------
// 0..3: mode/start-stop (wordt later door ControlTask verwerkt)
// 4..8: soft-keys rechts naast het scherm
// 10: encoder press (confirm)
// 11: encoder long press (cancel)
static constexpr uint32_t BTN_SOFT_1        = (1u << 4);
static constexpr uint32_t BTN_SOFT_2        = (1u << 5);
static constexpr uint32_t BTN_SOFT_3        = (1u << 6);
static constexpr uint32_t BTN_SOFT_4        = (1u << 7);
static constexpr uint32_t BTN_SOFT_5        = (1u << 8);
static constexpr uint32_t BTN_ENC_PRESS     = (1u << 10);
static constexpr uint32_t BTN_ENC_LONG      = (1u << 11);

static constexpr uint32_t DISPLAY_BTN_MASK =
    BTN_SOFT_1 | BTN_SOFT_2 | BTN_SOFT_3 | BTN_SOFT_4 | BTN_SOFT_5 | BTN_ENC_PRESS | BTN_ENC_LONG;

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
  Serial.println("lvgl_port_init: start");

  const uint16_t hor_res = 480;
  const uint16_t ver_res = 320;

  disp = lv_display_create(hor_res, ver_res);
  if (!disp) {
    Serial.println("ERROR: lv_display_create failed");
    return;
  }

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_flush_cb);

  // ---- Draw buffers (RGB565 => 2 bytes/pixel) ----
  static const uint16_t DRAW_BUF_LINES = 10;
  const size_t buf_pixels = (size_t)hor_res * (size_t)DRAW_BUF_LINES;
  const size_t buf_bytes  = buf_pixels * 2; // RGB565

  // 32-byte aligned + DMA-capable (veiligste keuze op ESP32 voor SPI flush)
  uint16_t* buf1 = (uint16_t*)heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  uint16_t* buf2 = (uint16_t*)heap_caps_aligned_alloc(32, buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

  Serial.printf("buf_bytes=%u heap=%lu buf1=%p buf2=%p\n",
                (unsigned)buf_bytes, (unsigned long)ESP.getFreeHeap(), buf1, buf2);

  if (!buf1 || !buf2) {
    Serial.println("ERROR: draw buffer alloc failed");
    if (buf1) heap_caps_free(buf1);
    if (buf2) heap_caps_free(buf2);
    return;
  }

  Serial.println("lvgl_port_init: before set_buffers");

  lv_display_set_buffers(
      disp,
      buf1,
      buf2,
      buf_bytes,                        // SIZE IN BYTES
      LV_DISPLAY_RENDER_MODE_PARTIAL
  );

  Serial.println("lvgl_port_init: after set_buffers");
  Serial.println("lvgl_port_init: done");
}

// ---------------- Curve select -> model ----------------
static void select_curve_into_model(UI1Model& ui1, const SystemSnapshot& s)
{
  ui1.curve_len = (int)s.curves.len;
  if (ui1.curve_len <= 0 || ui1.curve_len > CURVE_LEN) ui1.curve_len = CURVE_LEN;

  const int16_t* src = s.curves.curve0;
  if (s.ui.selected_curve_id == 1) src = s.curves.curve1;
  else if (s.ui.selected_curve_id == 2) src = s.curves.curve2;

  for (int i = 0; i < ui1.curve_len; ++i) ui1.curve[i] = src[i];

  int idx = (int)s.ui.start_index;
  if (idx < 0) idx = 0;
  if (idx > ui1.curve_len - 1) idx = ui1.curve_len - 1;
  ui1.progress_index = idx;
}

// ---------------- Mapping: SystemSnapshot -> DisplayModel ----------------
static void model_from_system(DisplayModel& m, const SystemSnapshot& s)
{
  const float vout = s.meas.v_out;

  float current = 0.0f;
  if (s.status.mode_current == POWER_MODE_SINK) current = s.meas.i_sink;
  else current = s.meas.i_source; // SOURCE of EMULATE

  // UI1 (Emulate)
  select_curve_into_model(m.ui1, s);
  m.ui1.voltage_val      = vout;
  m.ui1.current_val      = current;
  m.ui1.runtime_sec      = (uint32_t)(millis() / 1000);
  m.ui1.capacity_val     = s.ui.capacity_value;
  m.ui1.state_load       = (s.status.mode_current == POWER_MODE_SINK);
  m.ui1.nominal_v_val    = s.ui.nominal_voltage;
  m.ui1.btn_capacity_val = s.ui.capacity_value;

  // UI2 (Const Source)
  m.ui2.set_voltage = s.ui.ui2_set_voltage;
  m.ui2.meas_ampere = current;
  m.ui2.vmax        = 15.0f;

  // UI3 (Const Sink)
  m.ui3.set_ampere   = s.ui.ui3_set_current;
  m.ui3.meas_voltage = vout;
  m.ui3.imax         = 10.0f; // placeholder
}

// ---------------- UI create switch ----------------
static void clear_all_softkeys()
{
  ui1_softkey_clear_all();
  ui2_softkey_clear_all();
  ui3_softkey_clear_all();
}

static void switch_ui_if_needed(UiScreen requested)
{
  ActiveUI desired = current_ui;

  if (requested == UI_SCREEN_EMULATE) desired = ActiveUI::UI1;
  else if (requested == UI_SCREEN_CONST_SOURCE) desired = ActiveUI::UI2;
  else if (requested == UI_SCREEN_CONST_SINK) desired = ActiveUI::UI3;
  else desired = ActiveUI::UI1;

  if (desired == current_ui) return;

  current_ui = desired;
  clear_all_softkeys();
  ui_overlay_hide();

  switch (current_ui) {
    case ActiveUI::UI1: ui1_create(); break;
    case ActiveUI::UI2: ui2_create(); break;
    case ActiveUI::UI3: ui3_create(); break;
  }
}

// ---------------- Edit context ----------------
enum class EditField : uint8_t
{
  NONE = 0,
  UI1_CURVE,
  UI1_START_INDEX,
  UI1_NOMINAL_V,
  UI1_CAPACITY,
  UI2_SET_V,
  UI2_I_LIMIT,
  UI3_SET_I,
  UI3_V_LIMIT,
};

static EditField g_edit_field = EditField::NONE;
static int g_edit_softkey_idx = -1; // 0..4

// backups per edit (zodat cancel targetgericht kan terugzetten)
static uint8_t g_bak_u8 = 0;
static float   g_bak_f1 = 0.0f;

static UiEditField map_edit_field(EditField f)
{
  switch (f)
  {
    case EditField::UI1_CURVE:       return UI_EDIT_UI1_CURVE;
    case EditField::UI1_START_INDEX: return UI_EDIT_UI1_START_INDEX;
    case EditField::UI1_NOMINAL_V:   return UI_EDIT_UI1_NOMINAL_V;
    case EditField::UI1_CAPACITY:    return UI_EDIT_UI1_CAPACITY;
    case EditField::UI2_SET_V:       return UI_EDIT_UI2_SET_V;
    case EditField::UI2_I_LIMIT:     return UI_EDIT_UI2_I_LIMIT;
    case EditField::UI3_SET_I:       return UI_EDIT_UI3_SET_I;
    case EditField::UI3_V_LIMIT:     return UI_EDIT_UI3_V_LIMIT;
    default:                         return UI_EDIT_NONE;
  }
}

static void begin_edit(EditField field, int softkey_idx, const SystemSnapshot& s)
{
  g_edit_field = field;
  g_edit_softkey_idx = softkey_idx;

  // Backup wat we gaan aanpassen
  switch (field)
  {
    case EditField::UI1_CURVE:       g_bak_u8 = s.ui.selected_curve_id; break;
    case EditField::UI1_START_INDEX: g_bak_u8 = s.ui.start_index; break;
    case EditField::UI1_NOMINAL_V:   g_bak_f1 = s.ui.nominal_voltage; break;
    case EditField::UI1_CAPACITY:    g_bak_f1 = s.ui.capacity_value; break;
    case EditField::UI2_SET_V:       g_bak_f1 = s.ui.ui2_set_voltage; break;
    case EditField::UI2_I_LIMIT:     g_bak_f1 = s.ui.ui2_current_limit; break;
    case EditField::UI3_SET_I:       g_bak_f1 = s.ui.ui3_set_current; break;
    case EditField::UI3_V_LIMIT:     g_bak_f1 = s.ui.ui3_voltage_limit; break;
    default: break;
  }

  // UI highlight
  clear_all_softkeys();
  if (current_ui == ActiveUI::UI1) ui1_softkey_set_active(softkey_idx, true);
  if (current_ui == ActiveUI::UI2) ui2_softkey_set_active(softkey_idx, true);
  if (current_ui == ActiveUI::UI3) ui3_softkey_set_active(softkey_idx, true);

  // Overlay
  const char* hint = "Draai: wijzig | Press: OK | Long: Cancel";
  char title[32];
  char value[48];
  title[0] = 0; value[0] = 0;

  switch (field)
  {
    case EditField::UI1_CURVE:
      snprintf(title, sizeof(title), "Choose Curve");
      snprintf(value, sizeof(value), "Curve: %u", (unsigned)s.ui.selected_curve_id);
      break;
    case EditField::UI1_START_INDEX:
      snprintf(title, sizeof(title), "Choose Setpoint");
      snprintf(value, sizeof(value), "Start index: %u", (unsigned)s.ui.start_index);
      break;
    case EditField::UI1_NOMINAL_V:
      snprintf(title, sizeof(title), "Nominal voltage");
      snprintf(value, sizeof(value), "%.1f V", (double)s.ui.nominal_voltage);
      break;
    case EditField::UI1_CAPACITY:
      snprintf(title, sizeof(title), "Capacity");
      snprintf(value, sizeof(value), "%.1f F", (double)s.ui.capacity_value);
      break;
    case EditField::UI2_SET_V:
      snprintf(title, sizeof(title), "Voltage");
      snprintf(value, sizeof(value), "%.1f V", (double)s.ui.ui2_set_voltage);
      break;
    case EditField::UI2_I_LIMIT:
      snprintf(title, sizeof(title), "Current limit");
      snprintf(value, sizeof(value), "%.1f A", (double)s.ui.ui2_current_limit);
      break;
    case EditField::UI3_SET_I:
      snprintf(title, sizeof(title), "Ampere");
      snprintf(value, sizeof(value), "%.1f A", (double)s.ui.ui3_set_current);
      break;
    case EditField::UI3_V_LIMIT:
      snprintf(title, sizeof(title), "Voltage limit");
      snprintf(value, sizeof(value), "%.1f V", (double)s.ui.ui3_voltage_limit);
      break;
    default:
      snprintf(title, sizeof(title), "Edit");
      snprintf(value, sizeof(value), "");
      break;
  }

  ui_overlay_show(title, value, hint);

  // event naar ControlTask (later)
  UIEvents ev = s.ui_events;
  ev.flags |= UI_EVT_EDIT_STARTED;
  ev.field = map_edit_field(field);
  ev.seq++;
  system_write_ui_events(&ev);
}

static void end_edit(bool keep_values, const SystemSnapshot& s)
{
  // Revert indien cancel
  if (!keep_values)
  {
    UIShared ui = s.ui;
    switch (g_edit_field)
    {
      case EditField::UI1_CURVE:       ui.selected_curve_id = g_bak_u8; break;
      case EditField::UI1_START_INDEX: ui.start_index       = g_bak_u8; break;
      case EditField::UI1_NOMINAL_V:   ui.nominal_voltage   = g_bak_f1; break;
      case EditField::UI1_CAPACITY:    ui.capacity_value    = g_bak_f1; break;
      case EditField::UI2_SET_V:       ui.ui2_set_voltage   = g_bak_f1; break;
      case EditField::UI2_I_LIMIT:     ui.ui2_current_limit = g_bak_f1; break;
      case EditField::UI3_SET_I:       ui.ui3_set_current   = g_bak_f1; break;
      case EditField::UI3_V_LIMIT:     ui.ui3_voltage_limit = g_bak_f1; break;
      default: break;
    }
    system_write_ui_shared(&ui);

    UIEvents ev = s.ui_events;
    ev.flags |= UI_EVT_EDIT_CANCELLED;
    ev.field = map_edit_field(g_edit_field);
    ev.seq++;
    system_write_ui_events(&ev);
  }
  else
  {
    UIEvents ev = s.ui_events;
    ev.flags |= UI_EVT_EDIT_CONFIRMED;
    ev.field = map_edit_field(g_edit_field);
    ev.seq++;
    system_write_ui_events(&ev);
  }

  g_edit_field = EditField::NONE;
  g_edit_softkey_idx = -1;

  clear_all_softkeys();
  ui_overlay_hide();
}

static float clampf(float v, float lo, float hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void update_overlay_value(EditField field, const UIShared& ui)
{
  const char* hint = "Draai: wijzig | Press: OK | Long: Cancel";
  char title[32];
  char value[48];
  title[0] = 0; value[0] = 0;

  switch (field)
  {
    case EditField::UI1_CURVE:
      snprintf(title, sizeof(title), "Choose Curve");
      snprintf(value, sizeof(value), "Curve: %u", (unsigned)ui.selected_curve_id);
      break;
    case EditField::UI1_START_INDEX:
      snprintf(title, sizeof(title), "Choose Setpoint");
      snprintf(value, sizeof(value), "Start index: %u", (unsigned)ui.start_index);
      break;
    case EditField::UI1_NOMINAL_V:
      snprintf(title, sizeof(title), "Nominal voltage");
      snprintf(value, sizeof(value), "%.1f V", (double)ui.nominal_voltage);
      break;
    case EditField::UI1_CAPACITY:
      snprintf(title, sizeof(title), "Capacity");
      snprintf(value, sizeof(value), "%.1f F", (double)ui.capacity_value);
      break;
    case EditField::UI2_SET_V:
      snprintf(title, sizeof(title), "Voltage");
      snprintf(value, sizeof(value), "%.1f V", (double)ui.ui2_set_voltage);
      break;
    case EditField::UI2_I_LIMIT:
      snprintf(title, sizeof(title), "Current limit");
      snprintf(value, sizeof(value), "%.1f A", (double)ui.ui2_current_limit);
      break;
    case EditField::UI3_SET_I:
      snprintf(title, sizeof(title), "Ampere");
      snprintf(value, sizeof(value), "%.1f A", (double)ui.ui3_set_current);
      break;
    case EditField::UI3_V_LIMIT:
      snprintf(title, sizeof(title), "Voltage limit");
      snprintf(value, sizeof(value), "%.1f V", (double)ui.ui3_voltage_limit);
      break;
    default:
      return;
  }

  ui_overlay_update(title, value, hint);
}

static void do_reset_for_current_ui(const SystemSnapshot& s)
{
  UIShared ui = s.ui;
  switch (current_ui)
  {
    case ActiveUI::UI1:
      ui.selected_curve_id = 0;
      ui.start_index = 0;
      ui.nominal_voltage = 0.0f;
      ui.capacity_value = 0.0f;
      break;
    case ActiveUI::UI2:
      ui.ui2_set_voltage = 0.0f;
      ui.ui2_current_limit = 0.0f;
      break;
    case ActiveUI::UI3:
      ui.ui3_set_current = 0.0f;
      ui.ui3_voltage_limit = 0.0f;
      break;
  }

  system_write_ui_shared(&ui);

  UIEvents ev = s.ui_events;
  ev.flags |= UI_EVT_RESET_REQUESTED;
  ev.field = UI_EDIT_NONE;
  ev.seq++;
  system_write_ui_events(&ev);
}

static bool pressed(uint32_t changed_bits, uint32_t raw_bits, uint32_t mask)
{
  return (changed_bits & mask) && (raw_bits & mask);
}

static void handle_inputs(const SystemSnapshot& s)
{
  // Alleen in CONFIG nemen we UI-input over
  if (s.status.state != SYS_STATE_CONFIG)
  {
    if (g_edit_field != EditField::NONE)
      end_edit(true, s);
    return;
  }

  const uint32_t changed = s.io.buttons_changed_bits & DISPLAY_BTN_MASK;
  const uint32_t raw     = s.io.buttons_raw_bits & DISPLAY_BTN_MASK;
  const int32_t  enc_delta = s.io.enc_delta_accum;

  const bool soft1 = pressed(changed, raw, BTN_SOFT_1);
  const bool soft2 = pressed(changed, raw, BTN_SOFT_2);
  const bool soft3 = pressed(changed, raw, BTN_SOFT_3);
  const bool soft4 = pressed(changed, raw, BTN_SOFT_4);
  const bool soft5 = pressed(changed, raw, BTN_SOFT_5);

  const bool enc_press = pressed(changed, raw, BTN_ENC_PRESS);
  const bool enc_long  = pressed(changed, raw, BTN_ENC_LONG);

  // 1) Start edit als we nog niet editten
  if (g_edit_field == EditField::NONE)
  {
    if (soft5) { do_reset_for_current_ui(s); }

    if (current_ui == ActiveUI::UI1)
    {
      if (soft1) begin_edit(EditField::UI1_CURVE, 0, s);
      else if (soft2) begin_edit(EditField::UI1_START_INDEX, 1, s);
      else if (soft3) begin_edit(EditField::UI1_NOMINAL_V, 2, s);
      else if (soft4) begin_edit(EditField::UI1_CAPACITY, 3, s);
    }
    else if (current_ui == ActiveUI::UI2)
    {
      if (soft1) begin_edit(EditField::UI2_SET_V, 0, s);
      else if (soft2) begin_edit(EditField::UI2_I_LIMIT, 1, s);
    }
    else if (current_ui == ActiveUI::UI3)
    {
      if (soft1) begin_edit(EditField::UI3_SET_I, 0, s);
      else if (soft2) begin_edit(EditField::UI3_V_LIMIT, 1, s);
    }
  }
  else
  {
    // 2) Cancel/Confirm
    if (enc_long) {
      end_edit(false, s);
    } else if (enc_press) {
      end_edit(true, s);
    } else if (enc_delta != 0) {
      // 3) Encoder adjust
      UIShared ui = s.ui;
      bool changed_any = false;

      switch (g_edit_field)
      {
        case EditField::UI1_CURVE:
        {
          int v = (int)ui.selected_curve_id + (enc_delta > 0 ? 1 : -1);
          if (v < 0) v = 2;
          if (v > 2) v = 0;
          ui.selected_curve_id = (uint8_t)v;
          changed_any = true;
        } break;

        case EditField::UI1_START_INDEX:
        {
          int v = (int)ui.start_index + enc_delta;
          if (v < 0) v = 0;
          if (v > (CURVE_LEN - 1)) v = (CURVE_LEN - 1);
          ui.start_index = (uint8_t)v;
          changed_any = true;
        } break;

        case EditField::UI1_NOMINAL_V:
        {
          float v = ui.nominal_voltage + 0.1f * (float)enc_delta;
          ui.nominal_voltage = clampf(v, 0.0f, 15.0f);
          changed_any = true;
        } break;

        case EditField::UI1_CAPACITY:
        {
          float v = ui.capacity_value + 0.1f * (float)enc_delta;
          ui.capacity_value = clampf(v, 0.0f, 9999.9f);
          changed_any = true;
        } break;

        case EditField::UI2_SET_V:
        {
          float v = ui.ui2_set_voltage + 0.1f * (float)enc_delta;
          ui.ui2_set_voltage = clampf(v, 0.0f, 15.0f);
          changed_any = true;
        } break;

        case EditField::UI2_I_LIMIT:
        {
          float v = ui.ui2_current_limit + 0.1f * (float)enc_delta;
          ui.ui2_current_limit = clampf(v, 0.0f, 10.0f);
          changed_any = true;
        } break;

        case EditField::UI3_SET_I:
        {
          float v = ui.ui3_set_current + 0.1f * (float)enc_delta;
          ui.ui3_set_current = clampf(v, 0.0f, 10.0f);
          changed_any = true;
        } break;

        case EditField::UI3_V_LIMIT:
        {
          float v = ui.ui3_voltage_limit + 0.1f * (float)enc_delta;
          ui.ui3_voltage_limit = clampf(v, 0.0f, 15.0f);
          changed_any = true;
        } break;

        default: break;
      }

      if (changed_any)
      {
        system_write_ui_shared(&ui);
        update_overlay_value(g_edit_field, ui);

        UIEvents ev = s.ui_events;
        ev.flags |= UI_EVT_PARAM_CHANGED;
        ev.field = map_edit_field(g_edit_field);
        ev.seq++;
        system_write_ui_events(&ev);
      }
    }
  }

  // Consume inputs die display verwerkt
  if (changed) system_io_clear_buttons_changed(DISPLAY_BTN_MASK);
  if (enc_delta != 0) system_io_clear_enc_delta();
}

// ---------------- Task ----------------
void displayTask(void* pvParameters)
{
  (void)pvParameters;
  Serial.println("Display task gestart");

  backlight_init_and_on();
  ili9488_init();

  Serial.println("LVGL init start");

  lv_init();

  Serial.println("LVGL port init");

  lvgl_port_init();

  Serial.println("LVGL first screen");

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, "LVGL OK");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
  lv_refr_now(disp);          // force immediate refresh

  Serial.println("LVGL init done");

  // Start UI1
  current_ui = ActiveUI::UI1;
  ui1_create();

  const TickType_t period = pdMS_TO_TICKS(50); // 20 Hz
  TickType_t lastWake = xTaskGetTickCount();

  uint32_t last_lv_tick_ms = millis();

  while (true)
  {
    // LVGL tick
    uint32_t now_ms = millis();
    uint32_t dt = now_ms - last_lv_tick_ms;
    last_lv_tick_ms = now_ms;

    lv_tick_inc(dt);
    lv_timer_handler();
    //vTaskDelay(1);


    esp_task_wdt_reset();

    // Snapshot
    SystemSnapshot sys;
    system_read_snapshot(&sys);

    // UI switch op basis van system.ui.active_screen
    switch_ui_if_needed(sys.ui.active_screen);

    // Inputs verwerken (alleen in CONFIG)
    handle_inputs(sys);

    // model vullen + UI updaten
    model_from_system(g_model, sys);

    switch (current_ui) {
      case ActiveUI::UI1: ui1_update(g_model); break;
      case ActiveUI::UI2: ui2_update(g_model); break;
      case ActiveUI::UI3: ui3_update(g_model); break;
    }
    static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.println("display loop alive");
  }


    vTaskDelayUntil(&lastWake, period);
  }
}