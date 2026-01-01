// system/system.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =========================
// Enums
// =========================
typedef enum
{
    SYS_STATE_CONFIG = 0,
    SYS_STATE_READY,
    SYS_STATE_ACTIVE,
    SYS_STATE_ERROR
} SystemState;

typedef enum
{
    POWER_MODE_SOURCE = 0,
    POWER_MODE_SINK
} PowerMode;

// =========================
// Bitmasks
// =========================
enum
{
    FAULT_OV   = (1u << 0),
    FAULT_OC   = (1u << 1),
    FAULT_OT   = (1u << 2),
    FAULT_HW   = (1u << 3),
    FAULT_COMM = (1u << 4),
    FAULT_SD   = (1u << 5),
};

enum
{
    STATUS_CONTROL_ENABLED     = (1u << 0),
    STATUS_MODE_SWITCH_PENDING = (1u << 1),
    STATUS_ACTUATION_DIRTY     = (1u << 2),
    STATUS_LOG_BACKPRESSURE    = (1u << 3),
};

enum
{
    MEAS_ADC_OK        = (1u << 0),
    MEAS_ADC_SATURATED = (1u << 1),
    MEAS_RANGE_WARN    = (1u << 2),
};

enum
{
    APPLY_I2C_OK            = 0,
    APPLY_I2C_ERR_GENERIC   = (1u << 0),
    APPLY_I2C_ERR_RPOT      = (1u << 1),
    APPLY_I2C_ERR_MODE_SW   = (1u << 2),
    APPLY_I2C_ERR_BACKLIGHT = (1u << 3),
};

// =========================
// Curves
// =========================
#define CURVE_LEN 32

typedef struct
{
    uint16_t len;                 // = CURVE_LEN
    int16_t  curve0[CURVE_LEN];
    int16_t  curve1[CURVE_LEN];
    int16_t  curve2[CURVE_LEN];
} CurveData;

// =========================
// Shared data structs
// =========================
typedef struct
{
    uint32_t t_us;          // timestamp (micros)
    float    v_out;         // Vout = 5.333 * V_adc(AIN2)
    float    i_sink;        // Isink = (5/3) * V_adc(AIN1)
    float    i_source;      // Isource = (5/3) * V_adc(AIN3)
    float    temp_sink_c;   // temp = V_adc(AIN4) * (125/1.75)
    uint32_t meas_flags;    // MEAS_* flags
} MeasurementData;

typedef struct
{
    uint16_t pwm_duty;             // fast output (ESP32 PWM)
    uint16_t desired_rpot_code;    // slow output (I2C)
    PowerMode desired_mode;        // slow output (via MCP23008 over I2C)
    uint32_t control_flags;
} ControlData;

typedef struct
{
    uint16_t applied_rpot_code;
    PowerMode applied_mode;
    uint32_t apply_error_flags;
    uint32_t last_apply_t_ms;
} ApplyStatus;

typedef struct
{
    float set_voltage;
    float set_current;
    bool  logging_enabled;

    // 0..2 -> curve0/curve1/curve2
    uint8_t curve_id;
} ConfigData;

typedef struct
{
    SystemState state;

    PowerMode   mode_current;
    PowerMode   mode_pending;

    uint32_t status_flags;
    uint32_t fault_current_bits;
    uint32_t fault_latched_bits;
} SystemStatus;

typedef struct
{
    uint32_t buttons_raw_bits;
    uint32_t buttons_changed_bits;
    int32_t  enc_delta_accum;

    uint32_t led_output_bits;
    uint32_t mcp08_output_bits;
} IOShared;

typedef struct
{
    MeasurementData meas;
    ControlData     control;
    ApplyStatus     apply;
    ConfigData      cfg;
    SystemStatus    status;
    IOShared        io;

    CurveData       curves;

    uint32_t        seq;
} SystemData;

typedef SystemData SystemSnapshot;

// =========================
// System API
// =========================
void system_init(void);

void system_read_snapshot(SystemSnapshot* out_snapshot);

void system_write_measurement(const MeasurementData* meas);
void system_write_control(const ControlData* ctrl);
void system_write_apply_status(const ApplyStatus* apply);
void system_write_config(const ConfigData* cfg);
void system_write_status(const SystemStatus* status);
void system_write_io_shared(const IOShared* io);

void system_write_curves(const CurveData* curves);

void system_set_status_flag(uint32_t flag_bits);
void system_clear_status_flag(uint32_t flag_bits);

void system_set_fault_bits(uint32_t fault_bits);
void system_latch_fault_bits(uint32_t fault_bits);
void system_clear_latched_fault_bits(uint32_t fault_bits);

void system_io_clear_buttons_changed(uint32_t mask);
void system_io_clear_enc_delta(void);

void system_lock_data(void);
void system_unlock_data(void);

void system_lock_i2c(void);
void system_unlock_i2c(void);

#ifdef __cplusplus
} // extern "C"
#endif
