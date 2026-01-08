// ui_screens.hpp
#pragma once
#include <stdint.h>
#include <stdbool.h>

// --------------------
// Display model structs
// --------------------

// UI1 (Emulate)
struct UI1Model
{
    int16_t curve[32];       // curve values for chart (e.g. mV)
    int     curve_len;       // number of points (<=32)
    int     progress_index;  // marker index 0..curve_len-1

    float   voltage_val;     // measured V
    float   current_val;     // measured A

    // Note: marker capacity (CONFIG=start, RUN=now) in mAh
    float   capacity_val;    // mAh (position on curve)
    uint32_t runtime_sec;    // runtime in seconds

    bool    state_load;      // true=load/sink, false=unload/source

    // Config display (buttons)
    float   nominal_v_val;       // V (button)
    float   btn_capacity_val;    // mAh (total set capacity)
};

// UI2 (Constant source)
struct UI2Model
{
    // setpoints
    float set_voltage;      // V (setpoint)
    float current_limit;    // A (limit)

    // measurements
    float meas_voltage;     // V (optional)
    float meas_ampere;      // A (used in ui_screens.cpp)

    // gauge scale
    float vmax;             // V max for arc (used in ui_screens.cpp)
};

// UI3 (Constant sink)
struct UI3Model
{
    // setpoints
    float set_ampere;       // A (used in ui_screens.cpp)
    float voltage_limit;    // V limit

    // measurements
    float meas_voltage;     // V (used in ui_screens.cpp)
    float meas_current;     // A (optional)

    // gauge scale
    float imax;             // A max for arc (used in ui_screens.cpp)
};

struct DisplayModel
{
    UI1Model ui1;
    UI2Model ui2;
    UI3Model ui3;
};

// --------------------
// Screen lifecycle
// --------------------
void ui1_create();
void ui2_create();
void ui3_create();

void ui1_update(const DisplayModel& m);
void ui2_update(const DisplayModel& m);
void ui3_update(const DisplayModel& m);

// --------------------
// Softkey helpers (used by display.cpp)
// --------------------
void ui1_softkey_set_active(int idx, bool active); // idx 0..4
void ui2_softkey_set_active(int idx, bool active);
void ui3_softkey_set_active(int idx, bool active);

void ui1_softkey_clear_all();
void ui2_softkey_clear_all();
void ui3_softkey_clear_all();

// legacy (idx 1..5) if still needed
void ui1_set_softkey_highlight(uint8_t key_index, bool on);
void ui2_set_softkey_highlight(uint8_t key_index, bool on);
void ui3_set_softkey_highlight(uint8_t key_index, bool on);

void ui1_set_softkey_text(uint8_t key_index, const char* text);
void ui2_set_softkey_text(uint8_t key_index, const char* text);
void ui3_set_softkey_text(uint8_t key_index, const char* text);

// progress line update (callable externally; may remain static in cpp)
void ui1_update_progress_line(const DisplayModel& m);
void ui_overlay_hide();
bool ui_overlay_is_visible();
void ui_overlay_show(const char* title, const char* value, const char* hint);

