// measurement/measurement.cpp
#include <Arduino.h>
#include <SPI.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "system/system.h"
#include "measure/measure.h"

// =========================
// ADS8684 pinmapping (uit jouw schema)
// =========================
static constexpr int PIN_ADS_SCLK  = 38;
static constexpr int PIN_ADS_MISO  = 39;
static constexpr int PIN_ADS_MOSI  = 40;
static constexpr int PIN_ADS_CS    = 41;

// ADS_RESET: in je schema is er een netlabel "ADS_RESET" naar ESP32.
// Vul hier de juiste GPIO in zodra je hem zeker weet.
static constexpr int PIN_ADS_RESET = -1; // <-- AANPASSEN indien nodig

// =========================
// ADS8684 SPI
// =========================
static SPIClass SPI_ADS(FSPI);

// ADS8684 werkt typisch in SPI MODE1.
// Clock: begin conservatief (bijv. 5-10MHz) tot alles stabiel is.
static SPISettings ADS_SPI_SETTINGS(
    8000000,   // 8 MHz
    MSBFIRST,
    SPI_MODE1
);

// =========================
// Helpers
// =========================
static inline void ads_cs_low()  { digitalWrite(PIN_ADS_CS, LOW); }
static inline void ads_cs_high() { digitalWrite(PIN_ADS_CS, HIGH); }

static void ads_hw_reset()
{
    pinMode(PIN_ADS_RESET, OUTPUT);
    digitalWrite(PIN_ADS_RESET, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_ADS_RESET, HIGH);
    delay(5);
}

static void ads_spi_init()
{
    pinMode(PIN_ADS_CS, OUTPUT);
    ads_cs_high();

    SPI_ADS.begin(PIN_ADS_SCLK, PIN_ADS_MISO, PIN_ADS_MOSI, PIN_ADS_CS);

    // Eventueel: als je ADS8684 init commands wil sturen, doen we dat later hier.
    ads_hw_reset();
}

// Placeholder: leest één kanaal en geeft de ADC ingangsspanning in Volt terug.
// TODO: vervangen door echte ADS8684 SPI command/read flow.
static bool ads_read_channel_voltage(uint8_t ch, float* v_adc)
{
    if (!v_adc) return false;

    // --- PLACEHOLDER ---
    // Hier moet jij/ik later de echte ADS8684 communicatie plaatsen.
    // Voor nu geven we dummywaarden terug zodat de task werkt en je dataflow klopt.

    switch (ch)
    {
        case 0: *v_adc = 0.60f; break; // AIN1
        case 1: *v_adc = 1.20f; break; // AIN2
        case 2: *v_adc = 0.30f; break; // AIN3
        case 3: *v_adc = 1.00f; break; // AIN4
        default: *v_adc = 0.0f; return false;
    }
    return true;
}

// =========================
// Task
// =========================
extern "C" void measureTask(void* pvParameters)
{
    (void)pvParameters;

    ads_spi_init();

    // 1 kHz timing
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        // ===== WORK =====
        MeasurementData m{};
        m.t_us = (uint32_t)esp_timer_get_time();
        m.meas_flags = 0;

        float v_ain1 = 0.0f; // sink current sense input
        float v_ain2 = 0.0f; // vout sense input
        float v_ain3 = 0.0f; // source current sense input
        float v_ain4 = 0.0f; // temp sense input

        bool ok = true;

        // Mapping: CH0..CH3 == AIN1..AIN4 (zoals jij het beschreef)
        ok &= ads_read_channel_voltage(0, &v_ain1);
        ok &= ads_read_channel_voltage(1, &v_ain2);
        ok &= ads_read_channel_voltage(2, &v_ain3);
        ok &= ads_read_channel_voltage(3, &v_ain4);

        if (ok) m.meas_flags |= MEAS_ADC_OK;
        else    m.meas_flags |= MEAS_RANGE_WARN;

        // ===== Formules (jouw definities) =====
        // AIN1: I_sink = (5/3) * V
        m.i_sink = (5.0f / 3.0f) * v_ain1;

        // AIN2: Vout = 5.333 * V
        m.v_out = 5.333f * v_ain2;

        // AIN3: I_source = (5/3) * V
        m.i_source = (5.0f / 3.0f) * v_ain3;

        // AIN4: 125°C == 1.75V  => temp = V * (125/1.75)
        m.temp_sink_c = (125.0f / 1.75f) * v_ain4;

        // ===== WRITE =====
        system_write_measurement(&m);

        // ===== 1kHz pacing =====
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
    }
}
