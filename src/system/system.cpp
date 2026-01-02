// system/system.cpp
#include "system/system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

// interne opslag
static SystemData g_sys;
static SemaphoreHandle_t g_data_mutex = nullptr;
static SemaphoreHandle_t g_i2c_mutex  = nullptr;

static void init_default_curves(CurveData* c)
{
    if (!c) return;
    c->len = CURVE_LEN;

    // Realistische (ruwe) ontlaadcurves, genormaliseerd naar 0..100% van volle spanning.
    // X-as: capaciteit / SOC van 100% -> 0% (links->rechts).
    // Dit zijn "typische vormen" en geen datasheet-garanties.

    // Curve 0: Li-ion (NMC/18650) - snelle init drop, lange plateau, eind-sag
    const int16_t liion[CURVE_LEN] = {
        100,99,98,97,96,95,95,94,
        94,93,93,92,92,91,91,90,
        89,88,87,86,85,84,82,80,
        78,76,73,68,60,48,30,10
    };

    // Curve 1: LiFePO4 - zeer vlak plateau rond ~3.3V, daarna snelle drop
    const int16_t lifepo4[CURVE_LEN] = {
        100,99,99,98,98,97,97,96,
        96,96,95,95,95,94,94,94,
        93,93,93,92,92,92,91,90,
        88,85,80,70,55,38,20,8
    };

    // Curve 2: Lead-acid - meer lineaire sag
    const int16_t leadacid[CURVE_LEN] = {
        100,99,98,97,96,95,94,93,
        92,91,90,89,88,87,86,85,
        84,83,82,81,80,79,78,76,
        74,72,70,67,62,54,42,28
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

    // Curves + UI defaults
    init_default_curves(&g_sys.curves);

    g_sys.ui.active_screen     = UI_SCREEN_EMULATE;
    g_sys.ui.selected_curve_id = 0;
    g_sys.ui.start_index       = 0;
    g_sys.ui.nominal_voltage   = 4.20f;   // typische volle Li-ion spanning (1S)
    g_sys.ui.capacity_mAh   = 3000.0f;
    g_sys.ui.capacity_value = g_sys.ui.capacity_mAh;

    g_sys.ui.ui2_set_voltage   = 5.0f;
    g_sys.ui.ui2_current_limit = 2.0f;

    g_sys.ui.ui3_set_current   = 1.0f;
    g_sys.ui.ui3_voltage_limit = 12.0f;

    g_sys.ui_events.flags = UI_EVT_NONE;
    g_sys.ui_events.field = UI_EDIT_NONE;
    g_sys.ui_events.seq   = 0;

    g_sys.status.state        = SYS_STATE_CONFIG;
    g_sys.status.mode_current = POWER_MODE_EMULATE;
    g_sys.status.mode_pending = POWER_MODE_EMULATE;

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
