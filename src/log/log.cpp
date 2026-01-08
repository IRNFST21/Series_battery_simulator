#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "log/log.h"

// ===== UART (CP2102N) =====
static constexpr int LOG_UART_RX = 1;
static constexpr int LOG_UART_TX = 47;
static constexpr uint32_t LOG_UART_BAUD = 115200;
static HardwareSerial LogSerial(1);   // UART1

// ===== SD via SPI =====
static constexpr int SD_MISO = 8;
static constexpr int SD_MOSI = 9;
static constexpr int SD_SCLK = 18;
static constexpr int SD_CS   = 48;

// Use separate SPI bus instance (FSPI usually ok on S3)
static SPIClass spiSD(FSPI);

static File logFile;
static bool sd_ok = false;

// ===== Config =====
static constexpr uint32_t LOG_PERIOD_MS = 100;   // 10 Hz startpunt (later instelbaar)
static constexpr size_t   BUF_SIZE = 4096;       // RAM buffer for SD writes

static char   buf[BUF_SIZE];
static size_t buf_len = 0;

static void buf_append(const char* s)
{
    if (!s) return;
    size_t n = strlen(s);
    if (n == 0) return;

    // If too full: flush first
    if (buf_len + n >= BUF_SIZE) {
        if (sd_ok && logFile) {
            logFile.write((const uint8_t*)buf, buf_len);
            logFile.flush();
        }
        buf_len = 0;
    }
    memcpy(&buf[buf_len], s, n);
    buf_len += n;
}

static void buf_flush()
{
    if (buf_len == 0) return;
    if (sd_ok && logFile) {
        logFile.write((const uint8_t*)buf, buf_len);
        logFile.flush();
    }
    buf_len = 0;
}

static void uart_println(const char* s)
{
    if (!s) return;
    LogSerial.println(s);
}

// CSV header once
static void write_header()
{
    const char* header =
        "t_ms,state,mode,"
        "v_out,i_sink,i_source,tempC,"
        "screen,curve_id,nomV,capSet_mAh,start_mAh,"
        "capNow_mAh,runtime_s,"
        "pwm_duty,rpot_des,rpot_applied,apply_err\n";

    uart_println(header);
    buf_append(header);
    buf_flush();
}

static bool sd_init_open()
{
    // SD detect via system.io; for now we attempt init and fail gracefully
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, spiSD)) {
        return false;
    }

    // Unique name (simple): LOG00.CSV..LOG99.CSV
    char name[16];
    for (int i = 0; i < 100; ++i) {
        snprintf(name, sizeof(name), "/LOG%02d.CSV", i);
        if (!SD.exists(name)) {
            logFile = SD.open(name, FILE_WRITE);
            if (logFile) return true;
            return false;
        }
    }
    return false;
}

extern "C" void logTask(void* pvParameters)
{
    (void)pvParameters;
    Serial.println("logTask started");

    // UART init
    LogSerial.begin(LOG_UART_BAUD, SERIAL_8N1, LOG_UART_RX, LOG_UART_TX);

    // SD init (probe)
    sd_ok = sd_init_open();
    if (sd_ok) {
        Serial.println("SD OK: log file opened");
    } else {
        Serial.println("SD not available (yet)");
    }

    write_header();

    uint32_t last_sd_retry = millis();

    for (;;)
    {
        // Snapshot (copy!)
        SystemSnapshot s;
        system_read_snapshot(&s);

        // bool card_present = s.io.sd_present;  // example
        // For now: if SD not ok, retry every 2s
        if (!sd_ok) {
            if (millis() - last_sd_retry > 2000) {
                last_sd_retry = millis();
                sd_ok = sd_init_open();
                if (sd_ok) {
                    Serial.println("SD became available: opened log");
                    write_header();
                }
            }
        }

        // CSV line
        char line[256];
        snprintf(line, sizeof(line),
                 "%lu,%u,%u,"
                 "%.3f,%.3f,%.3f,%.2f,"
                 "%u,%u,%.2f,%u,%u,"
                 "%u,%lu,"
                 "%u,%u,%u,%u\n",
                 (unsigned long)millis(),
                 (unsigned)s.status.state,
                 (unsigned)s.status.mode_current,
                 (double)s.meas.v_out,
                 (double)s.meas.i_sink,
                 (double)s.meas.i_source,
                 (double)s.meas.temp_sink_c,
                 (unsigned)s.ui.active_screen,
                 (unsigned)s.ui.selected_curve_id,
                 (double)s.ui.nominal_voltage_V,
                 (unsigned)s.ui.capacity_set_mAh,
                 (unsigned)s.ui.start_capacity_mAh,
                 (unsigned)s.status.capacity_now_mAh,
                 (unsigned long)s.status.runtime_sec,
                 (unsigned)s.control.pwm_duty,
                 (unsigned)s.control.desired_rpot_code,
                 (unsigned)s.apply.applied_rpot_code,
                 (unsigned)s.apply.apply_error_flags);

        // UART direct
        LogSerial.print(line);

        // SD buffered
        buf_append(line);

        // Flush occasionally
        buf_flush();

        vTaskDelay(pdMS_TO_TICKS(LOG_PERIOD_MS));
    }
}
