// system/system.cpp
#include "system/system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// interne opslag
static SystemData g_sys;
static SemaphoreHandle_t g_data_mutex = nullptr;
static SemaphoreHandle_t g_i2c_mutex  = nullptr;

// Curves in permille (0..1000). X-as = capaciteit (0..capacity_set_mAh) gelijk verdeeld over CURVE_LEN.
static void init_default_curves(CurveData* c)
{
    if (!c) return;
    c->len = CURVE_LEN;

    // Curve 0: Li-ion (NMC) typische discharge shape
    const int16_t liion[CURVE_LEN] = {
        1000,995,990,985,980,975,970,968,
        965,962,960,957,955,952,950,947,
        942,935,925,910,895,875,850,820,
        790,760,720,670,600,500,320,120
    };

    // Curve 1: LiFePO4 vlak plateau, daarna snelle drop
    const int16_t lifepo4[CURVE_LEN] = {
        1000,998,996,994,992,990,988,986,
        985,984,983,982,981,980,979,978,
        977,976,975,974,972,970,965,955,
        930,890,820,720,560,400,220,80
    };

    // Curve 2: Lead-acid meer lineaire sag
    const int16_t leadacid[CURVE_LEN] = {
        1000,992,984,976,968,960,952,944,
        936,928,920,912,904,896,888,880,
        872,864,856,848,840,832,820,805,
        790,770,745,715,675,620,520,380
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

    // curves
    init_default_curves(&g_sys.curves);

    // UI defaults
    g_sys.ui.active_screen        = UI_SCREEN_UI1;
    g_sys.ui.selected_curve_id    = 0;
    g_sys.ui.nominal_voltage_V    = 12.0f;   // “nominal pack voltage” voor emulator
    g_sys.ui.capacity_set_mAh     = 3000;
    g_sys.ui.start_capacity_mAh   = 0;

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
