#pragma once

#include <stdint.h>
#include <stdbool.h>

// =========================
// Display model structs (data die displayTask aan de UI geeft)
// =========================

struct UI1Model {
  int16_t curve[32];
  int     curve_len;
  int     progress_index;

  float voltage_val;
  float current_val;
  float capacity_val;
  uint32_t runtime_sec;
  bool state_load;

  float nominal_v_val;
  float btn_capacity_val;
};

struct UI2Model {
  float set_voltage;
  float meas_ampere;
  float vmax;
};

struct UI3Model {
  float set_ampere;
  float meas_voltage;
  float imax;
};

struct DisplayModel {
  UI1Model ui1;
  UI2Model ui2;
  UI3Model ui3;
};

// =========================
// Screens
// =========================

void ui1_create();
void ui2_create();
void ui3_create();

void ui1_update(const DisplayModel& m);
void ui2_update(const DisplayModel& m);
void ui3_update(const DisplayModel& m);

// =========================
// UI helpers (softkey highlight + overlay)
// =========================

// Softkeys zijn altijd 5 stuks (rechts naast het scherm)
void ui1_softkey_set_active(int idx, bool active);
void ui2_softkey_set_active(int idx, bool active);
void ui3_softkey_set_active(int idx, bool active);

void ui1_softkey_clear_all();
void ui2_softkey_clear_all();
void ui3_softkey_clear_all();

// Overlay card (modal) in het midden (verschoven naar links vanwege sidebar)
void ui_overlay_show(const char* title, const char* value_line, const char* hint_line);
void ui_overlay_update(const char* title, const char* value_line, const char* hint_line);
void ui_overlay_hide();
bool ui_overlay_is_visible();
