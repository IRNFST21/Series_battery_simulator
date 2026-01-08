// ioexpander/ioExpander.cpp

#include <Arduino.h>
#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <Adafruit_MCP23X17.h>
#include <Adafruit_MCP23X08.h>

#include "system/system.h"
#include "ioexpander/ioexpander.h"

// ============================
// I2C devices
// ============================
static Adafruit_MCP23X17 mcp17;
static Adafruit_MCP23X08 mcp08;

static constexpr uint8_t MCP23017_ADDR = 0x20; // A2 A1 A0 = GND
static constexpr uint8_t MCP23008_ADDR = 0x21; // A2 A1 = GND, A0 = 3v3

// ============================
// MCP23017 mapping (Adafruit):
// 0..7 = GPA0..7, 8..15 = GPB0..7
// ============================

// Inputs (buttons) - active-low
static constexpr uint8_t PIN_EMULATE = 8;   // GPB0 -> bit 0
static constexpr uint8_t PIN_SINK    = 9;   // GPB1 -> bit 1
static constexpr uint8_t PIN_SOURCE  = 10;  // GPB2 -> bit 2
static constexpr uint8_t PIN_START   = 11;  // GPB3 -> bit 3

static constexpr uint8_t PIN_SK1     = 12;  // GPB4 -> bit 4
static constexpr uint8_t PIN_SK2     = 13;  // GPB5 -> bit 5
static constexpr uint8_t PIN_SK3     = 14;  // GPB6 -> bit 6
static constexpr uint8_t PIN_SK4     = 15;  // GPB7 -> bit 7
static constexpr uint8_t PIN_SK5     = 0;   // GPA0 -> bit 8

// Outputs (LEDs)
static constexpr uint8_t PIN_LED_EMUL = 1;  // GPA1
static constexpr uint8_t PIN_LED_SINK = 2;  // GPA2
static constexpr uint8_t PIN_LED_SRC  = 3;  // GPA3
static constexpr uint8_t PIN_LED_RUN  = 4;  // GPA4

// ============================
// MCP23008 (MCP23X08) pins
// ============================
static constexpr uint8_t PIN_SRC_ENABLE = 0; // GP0 output
static constexpr uint8_t PIN_LCD_RESET  = 1; // GP1 output
static constexpr uint8_t PIN_ENC_SW     = 2; // GP2 input
static constexpr uint8_t PIN_SD_CDWP    = 3; // GP3 input
static constexpr uint8_t PIN_FAN_MODE   = 4; // GP4 output

// ============================
// Encoder on ESP32
// ============================
static constexpr uint8_t ENC_A_GPIO = 6;
static constexpr uint8_t ENC_B_GPIO = 7;

// ============================
// system button bits
// ============================
static constexpr uint32_t BIT_EMULATE = (1u << 0);
static constexpr uint32_t BIT_SINK    = (1u << 1);
static constexpr uint32_t BIT_SOURCE  = (1u << 2);
static constexpr uint32_t BIT_START   = (1u << 3);

static constexpr uint32_t BIT_SK1     = (1u << 4);
static constexpr uint32_t BIT_SK2     = (1u << 5);
static constexpr uint32_t BIT_SK3     = (1u << 6);
static constexpr uint32_t BIT_SK4     = (1u << 7);
static constexpr uint32_t BIT_SK5     = (1u << 8);

static constexpr uint32_t BIT_ENC_PRESS = (1u << 10);
static constexpr uint32_t BIT_ENC_LONG  = (1u << 11);

static constexpr uint32_t BIT_SD_CDWP   = (1u << 20);

// ============================
// Debounce
// ============================
struct Debounce {
    uint32_t stable = 0;
    uint32_t last_sample = 0;
    uint8_t  count = 0;
};

static Debounce db;
static constexpr uint8_t DEBOUNCE_STABLE_COUNT = 4; // 4*5ms = 20ms

// ============================
// Encoder decode + long press
// ============================
static uint8_t enc_last_state = 0;

static bool enc_down = false;
static uint32_t enc_down_ms = 0;
static bool enc_long_sent = false;
static constexpr uint32_t ENC_LONG_MS = 800;

static int8_t decode_encoder_step(uint8_t a, uint8_t b)
{
    const uint8_t s = (a ? 1 : 0) | (b ? 2 : 0);
    const uint8_t prev = enc_last_state;
    enc_last_state = s;

    if (prev == 0 && s == 1) return +1;
    if (prev == 1 && s == 3) return +1;
    if (prev == 3 && s == 2) return +1;
    if (prev == 2 && s == 0) return +1;

    if (prev == 0 && s == 2) return -1;
    if (prev == 2 && s == 3) return -1;
    if (prev == 3 && s == 1) return -1;
    if (prev == 1 && s == 0) return -1;

    return 0;
}

static inline bool is_pressed_low(uint16_t gpio_bits, uint8_t pin)
{
    return ((gpio_bits & (1u << pin)) == 0);
}

static bool read_mcp23017_buttons(uint32_t* out_bits_pressed)
{
    if (!out_bits_pressed) return false;

    system_lock_i2c();
    const uint16_t gpio = mcp17.readGPIOAB();
    system_unlock_i2c();

    uint32_t b = 0;
    if (is_pressed_low(gpio, PIN_EMULATE)) b |= BIT_EMULATE;
    if (is_pressed_low(gpio, PIN_SINK))    b |= BIT_SINK;
    if (is_pressed_low(gpio, PIN_SOURCE))  b |= BIT_SOURCE;
    if (is_pressed_low(gpio, PIN_START))   b |= BIT_START;

    if (is_pressed_low(gpio, PIN_SK1)) b |= BIT_SK1;
    if (is_pressed_low(gpio, PIN_SK2)) b |= BIT_SK2;
    if (is_pressed_low(gpio, PIN_SK3)) b |= BIT_SK3;
    if (is_pressed_low(gpio, PIN_SK4)) b |= BIT_SK4;
    if (is_pressed_low(gpio, PIN_SK5)) b |= BIT_SK5;

    *out_bits_pressed = b;
    return true;
}

static bool read_mcp23008_inputs(bool* enc_sw_pressed, bool* sd_cdwp_low)
{
    if (!enc_sw_pressed || !sd_cdwp_low) return false;

    system_lock_i2c();
    const uint8_t g = mcp08.readGPIO();
    system_unlock_i2c();

    *enc_sw_pressed = ((g & (1u << PIN_ENC_SW)) == 0);   // active-low
    *sd_cdwp_low    = ((g & (1u << PIN_SD_CDWP)) == 0);  // active-low 
    return true;
}

static void update_outputs_from_system(const SystemSnapshot& s)
{
    const bool led_emul = (s.ui.active_screen == UI_SCREEN_UI1);
    const bool led_sink = (s.ui.active_screen == UI_SCREEN_UI3);
    const bool led_src  = (s.ui.active_screen == UI_SCREEN_UI2);
    const bool led_run  = (s.status.state == SYS_STATE_ACTIVE);

    system_lock_i2c();
    mcp17.digitalWrite(PIN_LED_EMUL, led_emul ? HIGH : LOW);
    mcp17.digitalWrite(PIN_LED_SINK, led_sink ? HIGH : LOW);
    mcp17.digitalWrite(PIN_LED_SRC,  led_src  ? HIGH : LOW);
    mcp17.digitalWrite(PIN_LED_RUN,  led_run  ? HIGH : LOW);
    system_unlock_i2c();

    const bool src_enable = (s.status.mode_current == POWER_MODE_SOURCE);
    const bool fan_hard   = (s.meas.temp_sink_c > 60.0f);

    system_lock_i2c();
    mcp08.digitalWrite(PIN_SRC_ENABLE, src_enable ? HIGH : LOW);
    mcp08.digitalWrite(PIN_LCD_RESET,  HIGH);
    mcp08.digitalWrite(PIN_FAN_MODE,   fan_hard ? HIGH : LOW);
    system_unlock_i2c();
}

static bool ioexpander_init()
{
    // Encoder GPIO
    pinMode(ENC_A_GPIO, INPUT_PULLUP);
    pinMode(ENC_B_GPIO, INPUT_PULLUP);
    enc_last_state = (digitalRead(ENC_A_GPIO) ? 1 : 0) | (digitalRead(ENC_B_GPIO) ? 2 : 0);

    // MCP23017
    system_lock_i2c();
    const bool ok17 = mcp17.begin_I2C(MCP23017_ADDR, &Wire);
    system_unlock_i2c();
    if (!ok17) {
        Serial.println("ERROR: MCP23017 not found");
        return false;
    }

    // Inputs with pullup (via INPUT_PULLUP)
    const uint8_t in_pins17[] = {PIN_EMULATE, PIN_SINK, PIN_SOURCE, PIN_START, PIN_SK1, PIN_SK2, PIN_SK3, PIN_SK4, PIN_SK5};

    system_lock_i2c();
    for (uint8_t p : in_pins17) {
        mcp17.pinMode(p, INPUT_PULLUP);
    }

    // LED outputs
    const uint8_t out_pins17[] = {PIN_LED_EMUL, PIN_LED_SINK, PIN_LED_SRC, PIN_LED_RUN};
    for (uint8_t p : out_pins17) {
        mcp17.pinMode(p, OUTPUT);
        mcp17.digitalWrite(p, LOW);
    }
    system_unlock_i2c();

    // MCP23008 (MCP23X08)
    system_lock_i2c();
    const bool ok08 = mcp08.begin_I2C(MCP23008_ADDR, &Wire);
    system_unlock_i2c();
    if (!ok08) {
        Serial.println("ERROR: MCP23008 not found");
        return false;
    }

    system_lock_i2c();
    // Outputs
    mcp08.pinMode(PIN_SRC_ENABLE, OUTPUT);
    mcp08.pinMode(PIN_LCD_RESET,  OUTPUT);
    mcp08.pinMode(PIN_FAN_MODE,   OUTPUT);

    mcp08.digitalWrite(PIN_SRC_ENABLE, LOW);
    mcp08.digitalWrite(PIN_LCD_RESET,  HIGH);
    mcp08.digitalWrite(PIN_FAN_MODE,   LOW);

    // Inputs with pullup (via INPUT_PULLUP)
    mcp08.pinMode(PIN_ENC_SW,  INPUT_PULLUP);  // <-- FIX
    mcp08.pinMode(PIN_SD_CDWP, INPUT_PULLUP);  // <-- FIX
    system_unlock_i2c();

    return true;
}

extern "C" void ioExpanderTask(void* pvParameters)
{
    (void)pvParameters;
    Serial.println("ioExpanderTask started");

    if (!ioexpander_init()) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    const TickType_t period = pdMS_TO_TICKS(5);
    TickType_t lastWake = xTaskGetTickCount();

    // Debounce baseline
    uint32_t b17 = 0;
    (void)read_mcp23017_buttons(&b17);

    bool enc_sw = false, sd_cdwp = false;
    (void)read_mcp23008_inputs(&enc_sw, &sd_cdwp);

    uint32_t s0 = b17;
    if (enc_sw)  s0 |= BIT_ENC_PRESS;
    if (sd_cdwp) s0 |= BIT_SD_CDWP;

    db.stable = s0;
    db.last_sample = s0;
    db.count = 0;

    for (;;)
    {
        // --- Read inputs ---
        uint32_t mcp17_bits = 0;
        bool enc_sw_pressed = false;
        bool sd_cdwp_low = false;

        (void)read_mcp23017_buttons(&mcp17_bits);
        (void)read_mcp23008_inputs(&enc_sw_pressed, &sd_cdwp_low);

        uint32_t sample = mcp17_bits;
        if (enc_sw_pressed) sample |= BIT_ENC_PRESS;
        if (sd_cdwp_low)    sample |= BIT_SD_CDWP;

        // --- Debounce ---
        if (sample != db.last_sample) {
            db.last_sample = sample;
            db.count = 0;
        } else {
            if (db.count < 255) db.count++;
        }

        uint32_t pressed_edges = 0;
        uint32_t stable_now = db.stable;

        if (db.count >= (DEBOUNCE_STABLE_COUNT - 1)) {
            if (db.stable != db.last_sample) {
                const uint32_t prev = db.stable;
                db.stable = db.last_sample;
                stable_now = db.stable;

                // rising edge => pressed event
                pressed_edges = (~prev) & db.stable;
            } else {
                stable_now = db.stable;
            }
        }

        // --- Encoder A/B ---
        int32_t enc_delta = 0;
        {
            const uint8_t a = digitalRead(ENC_A_GPIO) ? 1 : 0;
            const uint8_t b = digitalRead(ENC_B_GPIO) ? 1 : 0;
            enc_delta = decode_encoder_step(a, b);
        }

        // --- Encoder long press ---
        uint32_t long_event_changed = 0;
        uint32_t long_event_raw = 0;

        const bool enc_pressed_now = ((stable_now & BIT_ENC_PRESS) != 0);

        if (enc_pressed_now && !enc_down) {
            enc_down = true;
            enc_long_sent = false;
            enc_down_ms = millis();
        }
        if (!enc_pressed_now && enc_down) {
            enc_down = false;
            enc_long_sent = false;
        }

        if (enc_pressed_now && enc_down && !enc_long_sent) {
            const uint32_t held_ms = (uint32_t)(millis() - enc_down_ms);
            if (held_ms >= ENC_LONG_MS) {
                enc_long_sent = true;
                long_event_changed |= BIT_ENC_LONG;
                long_event_raw     |= BIT_ENC_LONG; // So raw&LONG is true in that cycle
            }
        }

        // --- Write to system.io ---
        SystemSnapshot snap;
        system_read_snapshot(&snap);

        IOShared io = snap.io;
        io.buttons_raw_bits = stable_now | long_event_raw;
        io.buttons_changed_bits |= (pressed_edges | long_event_changed);

        if (enc_delta != 0) io.enc_delta_accum += enc_delta;

        system_write_io_shared(&io);

        // --- Outputs ---
        system_read_snapshot(&snap);
        update_outputs_from_system(snap);

        vTaskDelayUntil(&lastWake, period);
    }
}
