// measure/measure.cpp
#include <Arduino.h>
#include <SPI.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "measure/measure.h"

// ================================
// ADS8684 SPI pin mapping (vul exact in volgens jouw schema)
// ================================
static constexpr int PIN_ADS_CS   = 10;
static constexpr int PIN_ADS_SCLK = 12;
static constexpr int PIN_ADS_MISO = 13;
static constexpr int PIN_ADS_MOSI = 11;

// In jouw setup wordt ADS_RESET niet gebruikt.
static constexpr int PIN_ADS_RESET = -1;

// ADS op SPI1
SPIClass SPI_ADS(FSPI);

static inline void ads_cs_low()  { digitalWrite(PIN_ADS_CS, LOW); }
static inline void ads_cs_high() { digitalWrite(PIN_ADS_CS, HIGH); }

static void ads_hw_reset()
{
    if (PIN_ADS_RESET < 0) {
        return; // geen reset-pin aangesloten
    }
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

extern "C" void measureTask(void* pvParameters)
{
    (void)pvParameters;

    Serial.println("measureTask started");
    ads_spi_init();

    const TickType_t period = pdMS_TO_TICKS(1); // 1 kHz
    TickType_t lastWake = xTaskGetTickCount();

    while (true)
    {
        float v_adc_ain1 = 0, v_adc_ain2 = 0, v_adc_ain3 = 0, v_adc_ain4 = 0;

        const bool ok1 = ads_read_channel_voltage(0, &v_adc_ain1);
        const bool ok2 = ads_read_channel_voltage(1, &v_adc_ain2);
        const bool ok3 = ads_read_channel_voltage(2, &v_adc_ain3);
        const bool ok4 = ads_read_channel_voltage(3, &v_adc_ain4);

        MeasurementData m{};
        m.t_us = (uint32_t)micros();

        // Formules:
        // AIN1 sink current: I = 5/3 * V
        // AIN2 voltage:      Vout = 5.333 * V
        // AIN3 source current: I = 5/3 * V
        // AIN4 temp: 125C == 1.75V -> T = V * (125/1.75)
        if (ok1) m.i_sink      = (5.0f / 3.0f) * v_adc_ain1;
        if (ok2) m.v_out       = 5.333f * v_adc_ain2;
        if (ok3) m.i_source    = (5.0f / 3.0f) * v_adc_ain3;
        if (ok4) m.temp_sink_c = v_adc_ain4 * (125.0f / 1.75f);

        m.meas_flags = 0;
        if (ok1 && ok2 && ok3 && ok4) m.meas_flags |= MEAS_ADC_OK;

        system_write_measurement(&m);

        vTaskDelayUntil(&lastWake, period);
    }
}
