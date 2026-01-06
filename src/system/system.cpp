// system/system.cpp

#include "system/system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Interne opslag
static SystemData g_sys;
static SemaphoreHandle_t g_data_mutex = nullptr;
static SemaphoreHandle_t g_i2c_mutex  = nullptr;

// =========================
// Defaults
// =========================

static void init_default_curves(CurveData* c)
{
    if (!c) return;
    c->len = CURVE_LEN;

    // 3 voorbeeld "battery-like" ontlaadcurves in voltage-% (0..100).
    // Interpretatie: Y% wordt geschaald met nominal_voltage_V.
    // X-as is uniforme stapjes over capaciteit.

    // Curve 0: Li-ion (relatief vlak plateau, dan drop)
    const int16_t c0[CURVE_LEN] = {
        100,100, 99, 99, 98, 98, 97, 97,
         96, 96, 95, 95, 94, 94, 93, 93,
         92, 92, 90, 88, 86, 84, 82, 80,
         78, 75, 72, 68, 62, 55, 45, 30
    };

    // Curve 1: "High-drain" (iets steilere daling over hele curve)
    const int16_t c1[CURVE_LEN] = {
        100, 99, 98, 97, 96, 95, 94, 93,
         92, 91, 90, 89, 88, 87, 86, 85,
         84, 83, 82, 81, 80, 78, 76, 74,
         72, 70, 68, 65, 60, 52, 40, 28
    };

    // Curve 2: "Lead-acid" (hoger begin, meer geleidelijke slope)
    const int16_t c2[CURVE_LEN] = {
        100,100,100, 99, 99, 98, 98, 97,
         97, 96, 96, 95, 95, 94, 94, 93,
         92, 91, 90, 89, 88, 86, 84, 82,
         80, 78, 75, 72, 68, 62, 52, 38
    };

    memcpy(c->curve0, c0, sizeof(c0));
    memcpy(c->curve1, c1, sizeof(c1));
    memcpy(c->curve2, c2, sizeof(c2));
}

static void init_default_ui(UIShared* ui)
{
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));

    ui->active_screen       = UI_SCREEN_UI1;
    ui->selected_curve_id   = 0;
    ui->nominal_voltage_V   = 12.0f;
    ui->capacity_set_mAh    = 2000; // default 2Ah
    ui->start_capacity_mAh  = 0;

    ui->ui2_set_voltage     = 5.0f;
    ui->ui2_current_limit   = 2.0f;

    ui->ui3_set_current     = 1.0f;
    ui->ui3_voltage_limit   = 12.0f;
}

static void init_default_status(SystemStatus* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));

    st->state        = SYS_STATE_CONFIG;
    st->mode_current = POWER_MODE_EMULATE;
    st->mode_pending = POWER_MODE_EMULATE;

    st->runtime_sec      = 0;
    st->capacity_now_mAh = 0;
}

// =========================
// API
// =========================

void system_init(void)
{
    if (g_data_mutex == nullptr) g_data_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex  == nullptr) g_i2c_mutex  = xSemaphoreCreateMutex();

    system_lock_data();
    memset(&g_sys, 0, sizeof(g_sys));

    init_default_curves(&g_sys.curves);
    init_default_ui(&g_sys.ui);
    init_default_status(&g_sys.status);

    // Defaults
    g_sys.cfg.set_voltage     = 0.0f;
    g_sys.cfg.set_current     = 0.0f;
    g_sys.cfg.logging_enabled = false;
    g_sys.cfg.curve_id        = 0;

    g_sys.control.pwm_duty          = 0;
    g_sys.control.desired_rpot_code = 0;
    g_sys.control.desired_mode      = POWER_MODE_EMULATE;

    g_sys.apply.applied_rpot_code = 0;
    g_sys.apply.applied_mode      = POWER_MODE_EMULATE;
    g_sys.apply.apply_error_flags = APPLY_I2C_OK;
    g_sys.apply.last_apply_t_ms   = 0;

    g_sys.ui_events.flags = UI_EVT_NONE;
    g_sys.ui_events.field = UI_EDIT_NONE;
    g_sys.ui_events.seq   = 0;

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
