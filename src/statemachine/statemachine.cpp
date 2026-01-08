// statemachine/statemachine.cpp
#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system/system.h"
#include "statemachine/statemachine.h"

static constexpr uint32_t BTN_MODE_EMULATE = (1u << 0);
static constexpr uint32_t BTN_MODE_SINK    = (1u << 1);
static constexpr uint32_t BTN_MODE_SOURCE  = (1u << 2);
static constexpr uint32_t BTN_START_PAUSE  = (1u << 3);

static constexpr uint32_t MODE_BTN_MASK =
    BTN_MODE_EMULATE | BTN_MODE_SINK | BTN_MODE_SOURCE | BTN_START_PAUSE;

static void apply_mode(SystemSnapshot& s, PowerMode mode, UiScreen screen)
{
    // UI screen
    s.ui.active_screen = screen;
    system_write_ui_shared(&s.ui);

    
    s.status.mode_pending = mode;
    s.status.mode_current = mode;
    system_write_status(&s.status);
}

static void handle_mode_buttons(SystemSnapshot& s, uint32_t changed)
{
    if (changed & BTN_MODE_EMULATE) {
        apply_mode(s, POWER_MODE_EMULATE, UI_SCREEN_UI1);
        return;
    }
    if (changed & BTN_MODE_SOURCE) {
        apply_mode(s, POWER_MODE_SOURCE, UI_SCREEN_UI2);
        return;
    }
    if (changed & BTN_MODE_SINK) {
        apply_mode(s, POWER_MODE_SINK, UI_SCREEN_UI3);
        return;
    }
}

static void handle_start_pause(SystemSnapshot& s, uint32_t changed)
{
    if ((changed & BTN_START_PAUSE) == 0) return;


    if (s.status.state == SYS_STATE_ACTIVE) {
        s.status.state = SYS_STATE_READY;
    } else if (s.status.state == SYS_STATE_READY) {
        s.status.state = SYS_STATE_ACTIVE;
    } else if (s.status.state == SYS_STATE_CONFIG) {
        s.status.state = SYS_STATE_READY;
    }
    system_write_status(&s.status);
}

extern "C" void statemachineTask(void* pvParameters)
{
    (void)pvParameters;
    Serial.println("statemachineTask started");

    const TickType_t period = pdMS_TO_TICKS(20); // 50 Hz
    TickType_t lastWake = xTaskGetTickCount();

    for (;;)
    {
        SystemSnapshot s;
        system_read_snapshot(&s);

        // Fault -> ERROR
        if (s.status.fault_latched_bits != 0 && s.status.state != SYS_STATE_ERROR) {
            s.status.state = SYS_STATE_ERROR;
            system_write_status(&s.status);
        }

        
        const uint32_t changed = s.io.buttons_changed_bits & MODE_BTN_MASK;
        if (changed != 0)
        {
            
            handle_mode_buttons(s, changed);
            handle_start_pause(s, changed);

            // clear consumed
            system_io_clear_buttons_changed(changed);
        }

        vTaskDelayUntil(&lastWake, period);
    }
}
