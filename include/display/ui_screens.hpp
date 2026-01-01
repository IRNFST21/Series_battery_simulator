#pragma once
#include <stdint.h>

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

void ui1_create();
void ui2_create();
void ui3_create();

void ui1_update(const DisplayModel& m);
void ui2_update(const DisplayModel& m);
void ui3_update(const DisplayModel& m);

// =========================
// Softkey helpers (grafisch)
// key_index: 1..5
// =========================
void ui1_set_softkey_highlight(uint8_t key_index, bool on);
void ui2_set_softkey_highlight(uint8_t key_index, bool on);
void ui3_set_softkey_highlight(uint8_t key_index, bool on);

void ui1_set_softkey_text(uint8_t key_index, const char* text);
void ui2_set_softkey_text(uint8_t key_index, const char* text);
void ui3_set_softkey_text(uint8_t key_index, const char* text);

// =========================
// Overlay (klein kaartje in het midden)
// =========================
void ui_overlay_show(const char* title, const char* value, const char* hint);
void ui_overlay_set_value(const char* value);
void ui_overlay_hide();
bool ui_overlay_is_visible();
