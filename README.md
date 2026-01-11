# Series Battery Simulator

## Overview

The Series Battery Simulator is an embedded project that implements a serial-battery emulation model on a microcontroller platform. It measures electrical signals, controls hardware outputs, and displays status and measurements via a graphical UI.

The codebase uses a modular architecture with a clear separation between hardware interaction, control logic and presentation.

## Features
- Battery behavior emulation (series configuration)
- Measurement and signal processing (voltage, current, temperature)
- GUI (LVGL) with multiple screens
- Mode/state management via a state machine
- UART + SD logging for debugging and data capture

## Architecture

Key design points:
- Modular subsystems with single responsibility
- Hardware abstraction layers for peripherals
- Central state machine for system behavior
- Separate display and UI logic
- Simple logging layer (UART/SD)

## Project Structure

Series_battery_simulator/
- src/
    - main.cpp                # Application entry
    - system/                 # Central data & initialization
        - system.cpp
    - statemachine/           # Mode/state handling
        - statemachine.cpp
    - measure/                # ADC / measurement logic
        - measure.cpp
    - display/                # LVGL UI + display driver
        - display.cpp
        - ui_screens.cpp
    - ioexpander/             # I/O expander (buttons, LEDs, encoder)
        - ioExpander.cpp
    - log/                    # UART/SD logging
        - log.cpp
- include/                  # Public headers
- test/                     # Test documentation / future tests

## Modules (short)
- `main.cpp` — system init and task creation (FreeRTOS)
- `system` — shared data structures and access API
- `statemachine` — handles mode transitions and errors
- `measure` — reads ADCs and converts to physical units
- `display` / `ui_screens` — LVGL screens and rendering
- `ioexpander` — reads buttons/encoder and controls LEDs/fan
- `log` — prints CSV lines over UART and writes to SD

## Technologies
- C++ (embedded)
- PlatformIO
- LVGL
- ESP32-class microcontroller (target)

## Development Principles
- Modular design
- Clear separation of hardware and logic
- Readable, testable modules

## Testing
The `test/` folder holds documentation for tests. The architecture is designed to allow adding unit and integration tests.

## Future Work
- Improved battery models (internal resistance, temp effects)
- Additional interfaces (USB, CAN)
- Remote logging / external storage
- Expanded fault handling and safety features

## Notes
This repository was developed as part of an embedded systems project with emphasis on architecture and maintainability.
