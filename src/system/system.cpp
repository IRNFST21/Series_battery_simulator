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

    // Defaults
    g_sys.cfg.set_voltage     = 0.0f;
    g_sys.cfg.set_current     = 0.0f;
    g_sys.cfg.logging_enabled = false;
    g_sys.cfg.curve_id        = 0;

    g_sys.control.pwm_duty          = 0;
    g_sys.control.desired_rpot_code = 0;
    g_sys.control.desired_mode      = POWER_MODE_SOURCE;

    g_sys.apply.applied_rpot_code = 0;
    g_sys.apply.applied_mode      = POWER_MODE_SOURCE;
    g_sys.apply.apply_error_flags = APPLY_I2C_OK;
    g_sys.apply.last_apply_t_ms   = 0;

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
