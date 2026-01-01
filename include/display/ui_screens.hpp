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
