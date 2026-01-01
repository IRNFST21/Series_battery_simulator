// system/system.cpp
#include "system/system.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// interne opslag
static SystemData g_sys;
static SemaphoreHandle_t g_data_mutex = nullptr;
static SemaphoreHandle_t g_i2c_mutex  = nullptr;

static void init_default_curves(CurveData* c)
{
    c->len = CURVE_LEN;

    // Voorbeeldcurves (0..100) zodat je chart meteen iets toont
    const int16_t c0[CURVE_LEN] = {
        100,98,96,94,92,90,88,86,84,82,80,78,76,74,72,70,
        68,66,64,62,60,58,56,54,52,50,48,46,44,42,40,38
    };
    const int16_t c1[CURVE_LEN] = {
        100,100,99,99,98,97,96,95,93,91,89,87,84,81,78,75,
        72,69,66,63,60,57,54,51,48,45,42,38,34,28,20,10
    };
    const int16_t c2[CURVE_LEN] = {
        100,100,100,100,99,99,99,98,98,97,97,96,96,95,95,94,
        93,92,90,88,85,82,78,74,70,65,60,54,48,40,30,15
    };

    memcpy(c->curve0, c0, sizeof(c0));
    memcpy(c->curve1, c1, sizeof(c1));
    memcpy(c->curve2, c2, sizeof(c2));
}

static void init_default_ui(UiSelection* ui)
{
    memset(ui, 0, sizeof(*ui));

    ui->active_screen = UI_SCREEN_EMULATE;

    ui->selected_curve_id = 0;
    ui->start_index       = 0;
    ui->nominal_voltage   = 12.0f;
    ui->capacity_value    = 1.00f;

    ui->ui2_set_voltage   = 5.0f;
    ui->ui2_current_limit = 2.0f;

    ui->ui3_set_current   = 1.0f;
    ui->ui3_voltage_limit = 12.0f;

    ui->ui1_reset_pulse = false;
    ui->ui2_reset_pulse = false;
    ui->ui3_reset_pulse = false;
}

void system_init(void)
{
    if (g_data_mutex == nullptr) g_data_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex  == nullptr) g_i2c_mutex  = xSemaphoreCreateMutex();

    system_lock_data();
    memset(&g_sys, 0, sizeof(g_sys));

    g_sys.status.state        = SYS_STATE_CONFIG;
    g_sys.status.mode_current = POWER_MODE_SOURCE;
    g_sys.status.mode_pending = POWER_MODE_SOURCE;

    // Veilig default: control disabled
    g_sys.status.status_flags = 0;

    // Defaults config
    g_sys.cfg.set_voltage     = 0.0f;
    g_sys.cfg.set_current     = 0.0f;
    g_sys.cfg.logging_enabled = false;
    g_sys.cfg.curve_id        = 0;

    // Control defaults
    g_sys.control.pwm_duty          = 0;
    g_sys.control.desired_rpot_code = 0;
    g_sys.control.desired_mode      = POWER_MODE_SOURCE;

    // Apply defaults
    g_sys.apply.applied_rpot_code = 0;
    g_sys.apply.applied_mode      = POWER_MODE_SOURCE;
    g_sys.apply.apply_error_flags = APPLY_I2C_OK;
    g_sys.apply.last_apply_t_ms   = 0;

    // Curves + UI defaults
    init_default_curves(&g_sys.curves);
    init_default_ui(&g_sys.ui);

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

void system_write_ui_selection(const UiSelection* ui)
{
    if (!ui) return;

    system_lock_data();
    g_sys.ui = *ui;
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
