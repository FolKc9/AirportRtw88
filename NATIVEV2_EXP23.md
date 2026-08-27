# NativeV2 final — Linux/Windows-style native defaults

This final build preserves the exp12 PCIe/RX/TX datapath and the exp22
EWMA, WPA2, and UserClient security fixes.

The final configuration additionally enables the normal rtw88 watchdog cycle,
uses shorter macOS NAPI moderation windows, and limits the IOKit output queue
to 64 packets to reduce bufferbloat while retaining aggregation.

- EWMA remains mathematically correct and unchanged.
- RSSI >= 46 receives a bounded +18 calibration (maximum 100).
- RSSI 38..45 receives a bounded +8 calibration.
- Weak RSSI below 38 is unchanged.
- `rssi_level` is derived from the calibrated value.

The matching CLI reports `EWMA`, `calibrated`, `level`, and `fw-rssi` so the
actual firmware input can be confirmed during a single 60-second test.
