// control/control.cpp
#include "control/control.h"

#include <Arduino.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"

// =========================
// Tuning / constants
// =========================
static constexpr float RPOT_VMAX = 16.0f;     // code 255 ~ 16V (jouw definitie)
static constexpr float SINK_IMAX = 5.0f;      // 0..5A

// Must match actuation PWM_RES_BITS=13 => 0..8191
static constexpr uint16_t PWM_MAX = 8191;

static constexpr float I_DEADBAND_A     = 0.05f; // Prevents oscillation near 0A
static constexpr float NEG_V_DEADBAND_V = 0.05f; // Charge trigger on negative V

static constexpr float TEMP_FAN_HIGH_C = 60.0f;
static constexpr float TEMP_FAN_LOW_C  = 50.0f;

// MCP23008 output bit mapping binnen system.io.mcp08_output_bits
// GP0 = enable source on power PCB
// GP4 = fan mode (hoog=hard, laag=zacht)
static constexpr uint32_t MCP08_BIT_SOURCE_EN = (1u << 0);
static constexpr uint32_t MCP08_BIT_FAN_MODE  = (1u << 4);

// =========================
// Helpers
// =========================
static inline float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline uint32_t clampu32(uint32_t x, uint32_t lo, uint32_t hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline uint16_t clampu16(int32_t x, int32_t lo, int32_t hi)
{
    if (x < lo) return (uint16_t)lo;
    if (x > hi) return (uint16_t)hi;
    return (uint16_t)x;
}

static const int16_t* select_curve_permille(const CurveData& c, uint8_t id)
{
    switch (id) {
        case 0: return c.curve0;
        case 1: return c.curve1;
        case 2: return c.curve2;
        default: return c.curve0;
    }
}

// A = remaining capacity
// remaining = capacity_set => curve[0] (vol)
// remaining = 0           => curve[last] (leeg)
static float curve_interp_permille_A(const CurveData& curves,
                                     uint8_t curve_id,
                                     uint32_t capacity_set_mAh,
                                     float capacity_now_mAh_f)
{
    if (capacity_set_mAh == 0) return 0.0f;

    const uint16_t n = (curves.len > 1) ? curves.len : CURVE_LEN;
    if (n < 2) return 0.0f;

    const int16_t* cv = select_curve_permille(curves, curve_id);

    float frac_remaining = capacity_now_mAh_f / (float)capacity_set_mAh; // 1..0
    frac_remaining = clampf(frac_remaining, 0.0f, 1.0f);

    // index: full -> 0, empty -> n-1
    float pos = (1.0f - frac_remaining) * (float)(n - 1);
    int i0 = (int)floorf(pos);
    int i1 = i0 + 1;
    if (i0 < 0) i0 = 0;
    if (i1 >= (int)n) i1 = (int)n - 1;

    float t  = pos - (float)i0;
    float v0 = (float)cv[i0];
    float v1 = (float)cv[i1];
    return v0 + (v1 - v0) * t; // permille 0..1000
}

static uint16_t voltage_to_rpot_code(float v_set)
{
    v_set = clampf(v_set, 0.0f, RPOT_VMAX);
    float code = (v_set / RPOT_VMAX) * 255.0f;
    return clampu16((int32_t)lroundf(code), 0, 255);
}

static uint16_t current_to_pwm_duty(float i_set)
{
    i_set = clampf(i_set, 0.0f, SINK_IMAX);
    float duty = (i_set / SINK_IMAX) * (float)PWM_MAX;
    return clampu16((int32_t)lroundf(duty), 0, (int32_t)PWM_MAX);
}

// =========================
// ControlTask
// =========================
void ControlTask(void* pvParameters)
{
    (void)pvParameters;

    // Emulate runtime state
    uint32_t last_tick_ms = millis();
    uint32_t runtime_ms_accum = 0;

    // Remaining capacity (float for 1kHz accumulation)
    float cap_now_mAh_f = 0.0f;
    bool cap_initialized = false;

    // UI event tracking
    uint32_t last_ui_ev_seq = 0;

    // Fan hysteresis
    bool fan_high = false;

    const TickType_t period = pdMS_TO_TICKS(1); // 1 kHz
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        SystemSnapshot sys;
        system_read_snapshot(&sys);

        // -------- UI reset event --------
        if (sys.ui_events.seq != last_ui_ev_seq) {
            last_ui_ev_seq = sys.ui_events.seq;

            if (sys.ui_events.flags & UI_EVT_RESET_REQUESTED) {
                uint32_t start_cap = clampu32(sys.ui.start_capacity_mAh, 0, sys.ui.capacity_set_mAh);
                cap_now_mAh_f = (float)start_cap;
                cap_initialized = true;
                runtime_ms_accum = 0;
            }
        }

        // Fan hysteresis control
        const float tC = sys.meas.temp_sink_c;
        if (!fan_high) {
            if (tC >= TEMP_FAN_HIGH_C) fan_high = true;
        } else {
            if (tC <= TEMP_FAN_LOW_C) fan_high = false;
        }

        // Build outputs
        ControlData ctrl = sys.control;
        SystemStatus st  = sys.status;
        IOShared io      = sys.io;

        // fan bit
        if (fan_high) io.mcp08_output_bits |= MCP08_BIT_FAN_MODE;
        else          io.mcp08_output_bits &= ~MCP08_BIT_FAN_MODE;

        // Calculate time delta
        uint32_t now_ms = millis();
        uint32_t dt_ms = now_ms - last_tick_ms;
        last_tick_ms = now_ms;
        if (dt_ms > 100) dt_ms = 100; // Clamp on stall

        const bool is_active = (sys.status.state == SYS_STATE_ACTIVE);

        // Safe defaults
        ctrl.pwm_duty = 0;
        ctrl.desired_rpot_code = 0;
        ctrl.desired_mode = sys.status.mode_current;

        // Over-temp check
        if (tC >= 85.0f) {
            system_set_fault_bits(FAULT_OT);
            system_latch_fault_bits(FAULT_OT);
            st.state = SYS_STATE_ERROR;
        }

        // Failsafe on error
        if (st.state == SYS_STATE_ERROR) {
            io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;
            ctrl.desired_mode = POWER_MODE_SINK;
            ctrl.pwm_duty = 0;
            ctrl.desired_rpot_code = 0;

            system_write_control(&ctrl);
            system_write_status(&st);
            system_write_io_shared(&io);

            vTaskDelayUntil(&lastWake, period);
            continue;
        }

        // ==========================================================
        // MODE: CONST SOURCE
        // ==========================================================
        if (sys.status.mode_current == POWER_MODE_SOURCE)
        {
            ctrl.desired_mode = POWER_MODE_SOURCE;

            if (!is_active) {
                io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;
                ctrl.desired_rpot_code = 0;
                ctrl.pwm_duty = 0;
            } else {
                io.mcp08_output_bits |= MCP08_BIT_SOURCE_EN;

                float vset = clampf(sys.ui.ui2_set_voltage, 0.0f, 15.0f);
                ctrl.desired_rpot_code = voltage_to_rpot_code(vset);
                ctrl.pwm_duty = 0; // Sink off (interlock)

                float ilim = clampf(sys.ui.ui2_current_limit, 0.0f, 5.0f);
                if (sys.meas.i_source > (ilim + 0.05f)) {
                    system_set_fault_bits(FAULT_OC);
                    system_latch_fault_bits(FAULT_OC);
                    st.state = SYS_STATE_ERROR;
                }
            }
        }

        // ==========================================================
        // MODE: CONST SINK
        // ==========================================================
        else if (sys.status.mode_current == POWER_MODE_SINK)
        {
            ctrl.desired_mode = POWER_MODE_SINK;

            // Source off (interlock)
            io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;
            ctrl.desired_rpot_code = 0;

            if (!is_active) {
                ctrl.pwm_duty = 0;
            } else {
                float iset = clampf(sys.ui.ui3_set_current, 0.0f, 5.0f);
                ctrl.pwm_duty = current_to_pwm_duty(iset);

                float vlim = clampf(sys.ui.ui3_voltage_limit, 0.0f, 15.0f);
                if (sys.meas.v_out > (vlim + 0.05f)) {
                    system_set_fault_bits(FAULT_OV);
                    system_latch_fault_bits(FAULT_OV);
                    st.state = SYS_STATE_ERROR;
                }
            }
        }

        // ==========================================================
        // MODE: EMULATE (batterij)
        // ==========================================================
        else if (sys.status.mode_current == POWER_MODE_EMULATE)
        {
            // init start capacity (A: remaining)
            if (!cap_initialized) {
                uint32_t start_cap = clampu32(sys.ui.start_capacity_mAh, 0, sys.ui.capacity_set_mAh);
                cap_now_mAh_f = (float)start_cap;
                cap_initialized = true;
                runtime_ms_accum = 0;
            }

            if (!is_active) {
                // Pause: all off
                io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;
                ctrl.desired_mode = POWER_MODE_EMULATE;
                ctrl.pwm_duty = 0;
                ctrl.desired_rpot_code = 0;
            } else {
                runtime_ms_accum += dt_ms;

                // Net current: >0=discharge, <0=charge
                float i_net = sys.meas.i_source - sys.meas.i_sink;

                // Charge trigger via negative voltage
                bool charge_by_v = (sys.meas.v_out < -NEG_V_DEADBAND_V);

                bool discharge = (i_net > I_DEADBAND_A) && !charge_by_v;
                bool charge    = (i_net < -I_DEADBAND_A) || charge_by_v;

                float dt_s = (float)dt_ms / 1000.0f;

                // Integrate capacity
                if (discharge) {
                    float d_mAh = (i_net * dt_s) * (1000.0f / 3600.0f);
                    cap_now_mAh_f -= d_mAh;
                } else if (charge) {
                    float ichg = fabsf(i_net);
                    float d_mAh = (ichg * dt_s) * (1000.0f / 3600.0f);
                    cap_now_mAh_f += d_mAh;
                }

                cap_now_mAh_f = clampf(cap_now_mAh_f, 0.0f, (float)sys.ui.capacity_set_mAh);

                // Lookup voltage from curve
                uint8_t curve_id = (sys.ui.selected_curve_id <= 2) ? sys.ui.selected_curve_id : 0;

                float permille = curve_interp_permille_A(sys.curves, curve_id,
                                                         sys.ui.capacity_set_mAh,
                                                         cap_now_mAh_f);

                float v_nom = clampf(sys.ui.nominal_voltage_V, 0.0f, 15.0f);
                float v_set = v_nom * (permille / 1000.0f);

                // Select mode based on current direction
                if (discharge) {
                    // Source on, sink off
                    ctrl.desired_mode = POWER_MODE_SOURCE;
                    io.mcp08_output_bits |= MCP08_BIT_SOURCE_EN;

                    ctrl.desired_rpot_code = voltage_to_rpot_code(v_set);
                    ctrl.pwm_duty = 0; // Sink off (interlock)
                }
                else if (charge) {
                    // Sink on, source off
                    ctrl.desired_mode = POWER_MODE_SINK;
                    io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;

                    // Absorb offered current (limited)
                    float i_absorb = clampf(fabsf(i_net), 0.0f, 5.0f);
                    ctrl.pwm_duty = current_to_pwm_duty(i_absorb);
                    ctrl.desired_rpot_code = 0; // Source off (interlock)
                }
                else {
                    // Near zero: off
                    ctrl.desired_mode = POWER_MODE_EMULATE;
                    io.mcp08_output_bits &= ~MCP08_BIT_SOURCE_EN;
                    ctrl.pwm_duty = 0;
                    ctrl.desired_rpot_code = 0;
                }

                // publish to status for UI
                st.runtime_sec = runtime_ms_accum / 1000u;
                st.capacity_now_mAh = (uint32_t)lroundf(cap_now_mAh_f);
            }
        }

        // leaving emulate: reset init flag
        if (sys.status.mode_current != POWER_MODE_EMULATE) {
            cap_initialized = false;
            runtime_ms_accum = 0;
        }

        // -------- write back --------
        system_write_control(&ctrl);
        system_write_status(&st);
        system_write_io_shared(&io);

        vTaskDelayUntil(&lastWake, period);
    }
}
