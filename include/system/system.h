// system/system.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =========================
// Constants
// =========================

// Default curve point count
#ifndef CURVE_LEN
#define CURVE_LEN 32
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
    POWER_MODE_SINK   = 1,
    POWER_MODE_EMULATE= 2
} PowerMode;

typedef enum
{
    UI_SCREEN_EMULATE = 0,
    UI_SCREEN_CONST_SOURCE,
    UI_SCREEN_CONST_SINK,
    UI_SCREEN_ERROR
} UiScreen;

typedef enum
{
    UI_EDIT_NONE = 0,

    // UI1
    UI_EDIT_UI1_CURVE,
    UI_EDIT_UI1_START_INDEX,
    UI_EDIT_UI1_NOMINAL_V,
    UI_EDIT_UI1_CAPACITY,

    // UI2
    UI_EDIT_UI2_SET_V,
    UI_EDIT_UI2_I_LIMIT,

    // UI3
    UI_EDIT_UI3_SET_I,
    UI_EDIT_UI3_V_LIMIT,
} UiEditField;

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

enum
{
    UI_EVT_NONE            = 0,
    UI_EVT_PARAM_CHANGED   = (1u << 0),
    UI_EVT_EDIT_STARTED    = (1u << 1),
    UI_EVT_EDIT_CONFIRMED  = (1u << 2),
    UI_EVT_EDIT_CANCELLED  = (1u << 3),
    UI_EVT_RESET_REQUESTED = (1u << 4),
};

// =========================
// Shared data structs
// =========================

// MeasurementData:
// AIN1: I_sink   = (5/3) * V_adc
// AIN2: V_out    = 5.333 * V_adc
// AIN3: I_source = (5/3) * V_adc
// AIN4: Temp_sink: 125C == 1.75V => temp = V_adc * (125/1.75)
typedef struct
{
    uint32_t t_us;        // timestamp (micros)
    float    v_out;       // output voltage (V)
    float    i_sink;      // sink current (A)
    float    i_source;    // source current (A)
    float    temp_sink_c; // sink temperature (°C)
    uint32_t meas_flags;  // MEAS_* flags
} MeasurementData;

typedef struct
{
    uint16_t pwm_duty;          // fast output (ESP32 PWM)
    uint16_t desired_rpot_code; // slow output (I2C)
    PowerMode desired_mode;     // slow output (via MCP23008 over I2C)
    uint32_t control_flags;
} ControlData;

typedef struct
{
    uint16_t applied_rpot_code;
    PowerMode applied_mode;
    uint32_t apply_error_flags;
    uint32_t last_apply_t_ms;
} ApplyStatus;

// "Control" setpoints (later door ControlTask gebruikt)
typedef struct
{
    float set_voltage;
    float set_current;
    bool  logging_enabled;
    uint8_t curve_id;
} ConfigData;

typedef struct
{
    int16_t  curve0[CURVE_LEN];
    int16_t  curve1[CURVE_LEN];
    int16_t  curve2[CURVE_LEN];
    uint16_t len; // altijd CURVE_LEN, maar expliciet voor veiligheid
} CurveData;

// UI-shared: alles wat de UI moet tonen en/of in CONFIG kan aanpassen.
typedef struct
{
    UiScreen active_screen;

    // UI1 (Emulate)
    uint8_t selected_curve_id; // 0..2
    uint8_t start_index;       // 0..(CURVE_LEN-1)
    float   nominal_voltage;   // 0..15 (step 0.1)
    float   capacity_value;    // F (step 0.1)

    // UI2 (Const Source)
    float ui2_set_voltage;     // 0..15 (step 0.1)
    float ui2_current_limit;   // A (step 0.1)

    // UI3 (Const Sink)
    float ui3_set_current;     // A (step 0.1)
    float ui3_voltage_limit;   // 0..15 (step 0.1)
} UIShared;

// UI events: displayTask kan hier intent in zetten; ControlTask kan dit later consumeren.
typedef struct
{
    uint32_t   flags;      // UI_EVT_* bitmask
    UiEditField field;     // welk veld was relevant
    uint32_t   seq;        // monotonic counter
} UIEvents;

typedef struct
{
    SystemState state;
    PowerMode   mode_current;
    PowerMode   mode_pending;

    uint32_t status_flags;
    uint32_t fault_current_bits;
    uint32_t fault_latched_bits;
} SystemStatus;

// I/O snapshot: knoppen + encoder + outputs.
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
    UIShared        ui;
    UIEvents        ui_events;

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
void system_write_ui_shared(const UIShared* ui);
void system_write_ui_events(const UIEvents* ev);

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
