// system/system.cpp
#include "system/system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Internal storage
static SystemData g_sys;
static SemaphoreHandle_t g_data_mutex = nullptr;
static SemaphoreHandle_t g_i2c_mutex  = nullptr;

// Curves in permille (0..1000). X-axis = capacity (0..capacity_set_mAh) equally spaced over CURVE_LEN.
static void init_default_curves(CurveData* c)
{
    if (!c) return;
    c->len = CURVE_LEN;

    // Curve 0: Li-ion (1S) discharge shape (typical NMC/LCO)
    // Shape: quick drop -> plateau -> knee.
    // Normalized to Vmax (e.g. 4.2V). End ~0.714 (3.0/4.2).
    const int16_t liion[CURVE_LEN] = {
        1000, 977, 962, 958, 954, 949, 945, 941,
         936, 932, 928, 923, 919, 915, 910, 906,
         902, 898, 893, 889, 885, 880, 876, 872,
         867, 863, 859, 837, 806, 776, 745, 714
    };

    // Curve 1: LiFePO4 (1S) flat plateau, then clear knee.
    // Normalized to Vmax (e.g. 3.65V). End ~0.767 (2.8/3.65).
    const int16_t lifepo4[CURVE_LEN] = {
        1000, 945, 943, 941, 939, 937, 935, 933,
         931, 929, 927, 925, 923, 921, 919, 917,
         915, 913, 911, 908, 906, 904, 902, 900,
         898, 896, 894, 892, 886, 847, 807, 767
    };

    // Curve 2: Lead-acid (2V cell) more linear sag, mild plateau, then drop.
    // Normalized to Vmax (2.12V). End ~0.825 (1.75/2.12).
    const int16_t leadacid[CURVE_LEN] = {
        1000, 991, 982, 973, 969, 966, 963, 960,
         957, 954, 951, 948, 945, 942, 939, 935,
         932, 929, 926, 923, 920, 917, 914, 911,
         908, 903, 890, 877, 864, 851, 838, 825
    };

    memcpy(c->curve0, liion, sizeof(liion));
    memcpy(c->curve1, lifepo4, sizeof(lifepo4));
    memcpy(c->curve2, leadacid, sizeof(leadacid));
}

void system_init(void)
{
    if (g_data_mutex == nullptr) g_data_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex  == nullptr) g_i2c_mutex  = xSemaphoreCreateMutex();

    system_lock_data();
    memset(&g_sys, 0, sizeof(g_sys));

    // Curves
    init_default_curves(&g_sys.curves);

    // UI defaults
    g_sys.ui.active_screen = UI_SCREEN_UI1;
    g_sys.ui.selected_curve_id = 0;
    g_sys.ui.nominal_voltage_V = 12.0f;   // "nominal pack voltage" for emulator
    g_sys.ui.capacity_set_mAh = 3000;
    g_sys.ui.start_capacity_mAh = 0;

    g_sys.ui.ui2_set_voltage      = 5.0f;
    g_sys.ui.ui2_current_limit    = 2.0f;

    g_sys.ui.ui3_set_current      = 1.0f;
    g_sys.ui.ui3_voltage_limit    = 12.0f;

    // UI events
    g_sys.ui_events.flags = UI_EVT_NONE;
    g_sys.ui_events.field = UI_EDIT_NONE;
    g_sys.ui_events.seq   = 0;

    // Status defaults
    g_sys.status.state             = SYS_STATE_CONFIG;
    g_sys.status.mode_current      = POWER_MODE_EMULATE;
    g_sys.status.mode_pending      = POWER_MODE_EMULATE;

    g_sys.status.status_flags      = 0;
    g_sys.status.fault_current_bits = 0;
    g_sys.status.fault_latched_bits = 0;

    g_sys.status.runtime_sec       = 0;
    g_sys.status.capacity_now_mAh  = 0;

    g_sys.seq = 0;

    system_unlock_data();
}

void system_read_snapshot(SystemSnapshot* out_snapshot)
{
    if (!out_snapshot) return;
    system_lock_data();
    memcpy(out_snapshot, &g_sys, sizeof(SystemSnapshot));
    system_unlock_data();
}

void system_write_measurement(const MeasurementData* meas)
{
    if (!meas) return;
    system_lock_data();
    g_sys.meas = *meas;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_control(const ControlData* ctrl)
{
    if (!ctrl) return;
    system_lock_data();
    g_sys.control = *ctrl;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_apply_status(const ApplyStatus* apply)
{
    if (!apply) return;
    system_lock_data();
    g_sys.apply = *apply;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_config(const ConfigData* cfg)
{
    if (!cfg) return;
    system_lock_data();
    g_sys.cfg = *cfg;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_status(const SystemStatus* status)
{
    if (!status) return;
    system_lock_data();
    g_sys.status = *status;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_io_shared(const IOShared* io)
{
    if (!io) return;
    system_lock_data();
    g_sys.io = *io;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_curves(const CurveData* curves)
{
    if (!curves) return;
    system_lock_data();
    g_sys.curves = *curves;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_ui_shared(const UIShared* ui)
{
    if (!ui) return;
    system_lock_data();
    g_sys.ui = *ui;
    g_sys.seq++;
    system_unlock_data();
}

void system_write_ui_events(const UIEvents* ev)
{
    if (!ev) return;
    system_lock_data();
    g_sys.ui_events = *ev;
    g_sys.seq++;
    system_unlock_data();
}

void system_set_status_flag(uint32_t flag_bits)
{
    system_lock_data();
    g_sys.status.status_flags |= flag_bits;
    g_sys.seq++;
    system_unlock_data();
}

void system_clear_status_flag(uint32_t flag_bits)
{
    system_lock_data();
    g_sys.status.status_flags &= ~flag_bits;
    g_sys.seq++;
    system_unlock_data();
}

void system_set_fault_bits(uint32_t fault_bits)
{
    system_lock_data();
    g_sys.status.fault_current_bits |= fault_bits;
    g_sys.seq++;
    system_unlock_data();
}

void system_latch_fault_bits(uint32_t fault_bits)
{
    system_lock_data();
    g_sys.status.fault_current_bits |= fault_bits;
    g_sys.status.fault_latched_bits |= fault_bits;
    g_sys.seq++;
    system_unlock_data();
}

void system_clear_latched_fault_bits(uint32_t fault_bits)
{
    system_lock_data();
    g_sys.status.fault_latched_bits &= ~fault_bits;
    g_sys.seq++;
    system_unlock_data();
}

void system_io_clear_buttons_changed(uint32_t mask)
{
    system_lock_data();
    g_sys.io.buttons_changed_bits &= ~mask;
    g_sys.seq++;
    system_unlock_data();
}

void system_io_clear_enc_delta(void)
{
    system_lock_data();
    g_sys.io.enc_delta_accum = 0;
    g_sys.seq++;
    system_unlock_data();
}

void system_lock_data(void)
{
    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
}

void system_unlock_data(void)
{
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
}

void system_lock_i2c(void)
{
    if (g_i2c_mutex) xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);
}

void system_unlock_i2c(void)
{
    if (g_i2c_mutex) xSemaphoreGive(g_i2c_mutex);
}
