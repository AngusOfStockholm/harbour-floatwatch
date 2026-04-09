# harbour-floatwatch

Sailfish OS application for monitoring VESC telemetry over Bluetooth (BLE).

## What this is

This app connects to a VESC-based device (e.g. Funwheel) using BLE and reads live telemetry such as voltage and speed.

The focus of the project is:
- reliable communication with VESC over BLE
- simple, readable architecture
- practical use (battery monitoring)

## Features

- Connect to BLE device via BlueZ (DBus)
- Read live voltage
- Display voltage on main UI and cover
- Show voltage delta between updates
- Manual refresh from UI and cover
- Device selection persists between sessions
- Speed field present (not yet correctly implemented)

## Implementation notes

- BLE handled via BlueZ DBus (not QtBluetooth)
- Connection initiated via `bluetoothctl` (QProcess)
- Communication uses Nordic UART Service (NUS)
- VESC commands sent using `COMM_GET_VALUES`
- Polling loop (~200 ms) used for updates
- Voltage decoding implemented; speed requires ERPM → km/h conversion (not yet implemented)

## Limitations

- Uses polling instead of event-driven updates
- Background execution is limited by Sailfish OS
- Sailjail compatibility not finalized
- Connection approach (bluetoothctl) is not ideal long-term
- Speed is not yet correctly calculated (requires ERPM conversion using VESC configuration)

## Status

Working prototype.

Core telemetry (voltage) is stable and usable.

## Next steps

- Voltage target alarm (primary goal)
- Improve connection robustness
- Evaluate Sailjail-compatible BLE approach

## Author

Andrew (AngusOfStockholm)
