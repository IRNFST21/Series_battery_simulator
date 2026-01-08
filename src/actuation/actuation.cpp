// actuation/actuation.cpp
#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "actuation/actuation.h"

// =====================
// PWM (Sink) settings
// =====================
static constexpr int PWM_GPIO       = 4;
static constexpr int PWM_CHANNEL    = 0;
static constexpr int PWM_RES_BITS   = 13;            // 0..8191
static constexpr int PWM_MAX_DUTY   = (1 << PWM_RES_BITS) - 1;


static constexpr int SINK_PWM_FREQ_HZ = 1;

// =====================
// AD5274 (Source) settings
// =====================
#ifndef AD5274_I2C_ADDR
#define AD5274_I2C_ADDR 0x2C
#endif

// AD5274 write RDAC:
// 16-bit: [C3..C0 | D9..D0]
// cmd=0x1 (RDAC write), 8-bit code -> 10-bit (<<2)
static bool ad5274_write_rdac(uint8_t code)
{
    uint16_t data10 = ((uint16_t)code) << 2;   // 8-bit → 10-bit
    uint16_t cmd    = (0x1u << 12) | (data10 & 0x03FFu);

    system_lock_i2c();

    Wire.beginTransmission(AD5274_I2C_ADDR);
    Wire.write((uint8_t)(cmd >> 8));
    Wire.write((uint8_t)(cmd & 0xFF));
    uint8_t err = Wire.endTransmission();

    system_unlock_i2c();

    return (err == 0);
}

static void pwm_init()
{
    ledcSetup(PWM_CHANNEL, SINK_PWM_FREQ_HZ, PWM_RES_BITS);
    ledcAttachPin(PWM_GPIO, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
}

static inline void pwm_off()
{
    ledcWrite(PWM_CHANNEL, 0);
}

static inline void pwm_set_u16(uint16_t duty_u16)
{
    int duty = (int)duty_u16;
    if (duty < 0) duty = 0;
    if (duty > PWM_MAX_DUTY) duty = PWM_MAX_DUTY;
    ledcWrite(PWM_CHANNEL, duty);
}

extern "C" void actuationTask(void* pvParameters)
{
    (void)pvParameters;
    Serial.println("actuationTask started");

    pwm_init();

    ApplyStatus apply = {};
    apply.applied_rpot_code  = 0;
    apply.applied_mode       = POWER_MODE_EMULATE; // init state
    apply.apply_error_flags  = APPLY_I2C_OK;
    apply.last_apply_t_ms    = millis();
    system_write_apply_status(&apply);

    uint8_t last_rpot_code = 0xFF;

    const TickType_t period = pdMS_TO_TICKS(1); // 1 kHz
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        SystemSnapshot s;
        system_read_snapshot(&s);

        const ControlData ctrl = s.control;

        uint32_t err_flags = APPLY_I2C_OK;

        // Determine effective actuation direction
        // Interlock: no simultaneous source/sink
        PowerMode effective = ctrl.desired_mode;

        // If EMULATE, infer from setpoints
        if (effective == POWER_MODE_EMULATE)
        {
            if (ctrl.pwm_duty > 0)            effective = POWER_MODE_SINK;
            else if (ctrl.desired_rpot_code > 0) effective = POWER_MODE_SOURCE;
            else                               effective = POWER_MODE_EMULATE; // OFF
        }

        apply.applied_mode = effective;

        // ------------------------------------------------
        // Apply commands
        // ------------------------------------------------
        if (effective == POWER_MODE_SOURCE)
        {
            // Sink off (interlock)
            pwm_off();

            // RPOT code clamp
            uint16_t code16 = ctrl.desired_rpot_code;
            uint8_t  code8  = (code16 > 255) ? 255 : (uint8_t)code16;

            // Only write if changed
            if (code8 != last_rpot_code)
            {
                if (!ad5274_write_rdac(code8)) {
                    err_flags |= APPLY_I2C_ERR_RPOT;
                } else {
                    last_rpot_code = code8;
                    apply.applied_rpot_code = code8;
                }
            }
        }
        else if (effective == POWER_MODE_SINK)
        {
            // Source off via HW (MCP23008 GP0 - IOExpanderTask)
            // Sink PWM only
            pwm_set_u16(ctrl.pwm_duty);
        }
        else
        {
            // Off
            pwm_off();
            // RPOT untouched, source already off
        }

        apply.apply_error_flags = err_flags;
        apply.last_apply_t_ms = millis();
        system_write_apply_status(&apply);

        vTaskDelayUntil(&lastWake, period);
    }
}
