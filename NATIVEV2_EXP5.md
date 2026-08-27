# NativeV2 exp5 — RA RSSI Feed

exp4 showed VHT 1SS / 80 MHz with excellent host RSSI, but firmware TX stayed
at VHT MCS0. exp5 fixes the missing station/RSSI feedback path.

## Changes

- Registers the single associated `ieee80211_sta` in the mac80211 shim.
- Implements `ieee80211_iterate_stations_atomic()`.
- Implements `ieee80211_find_sta()` and `ieee80211_find_sta_by_ifaddr()`.
- Preserves Feixiao's disabled full RF-dynamic watchdog, but adds a minimal
  RSSI-only watchdog feed to `rtw_fw_send_rssi_info()` through upstream rtw88.
- Adds `exp5 RA RSSI feed` diagnostics to `rtw88ctl perf`.

Expected healthy signs are `bridge=yes`, non-zero `EWMA`, increasing `sta-find`
and `sta-iter`, followed by firmware TX moving above `VHT1SS-MCS0` if this was
the rate-adaptation limiter.


Build-package fix: the exp5 RA feed is now applied by an idempotent source editor instead of a fragile git patch, so a fresh default-branch rtw88-stable clone builds deterministically.
