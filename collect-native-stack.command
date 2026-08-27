#!/bin/bash
set -u
cd "$(dirname "$0")"
OUT="native-stack-report.txt"
{
  echo "Feixiao NativeV2 stack report"
  echo "Generated: $(date)"
  echo
  echo "=== macOS ==="
  sw_vers 2>&1
  uname -a 2>&1
  echo
  echo "=== boot args / SIP ==="
  nvram boot-args 2>&1 || true
  csrutil status 2>&1 || true
  echo
  echo "=== loaded Wi-Fi/network kexts ==="
  kmutil showloaded 2>&1 | grep -Ei 'rtw88|IO80211|Airport|Skywalk|itlwm|Lilu|AMFIPass' || true
  echo
  echo "=== IO80211 / Skywalk bundles on disk ==="
  for p in \
    /System/Library/Extensions/IO80211Family.kext \
    /System/Library/Extensions/IOSkywalkFamily.kext \
    /System/Library/Extensions/IO80211FamilyLegacy.kext; do
    if [ -e "$p" ]; then
      echo "-- $p"
      /usr/bin/plutil -p "$p/Contents/Info.plist" 2>&1 | head -120 || true
    else
      echo "-- MISSING: $p"
    fi
  done
  echo
  echo "=== network interfaces ==="
  ifconfig -a 2>&1
  echo
  echo "=== AirPort / Wi-Fi system profiler ==="
  system_profiler SPAirPortDataType 2>&1 || true
  echo
  echo "=== matching IOKit services ==="
  ioreg -r -l -w0 2>&1 | grep -Ei -C 3 'RTW88|RTL8821|IO80211|AirPort|Skywalk' | head -1200 || true
  echo
  echo "=== recent driver messages ==="
  log show --last boot --style compact \
    --predicate 'process == "kernel" AND (eventMessage CONTAINS[c] "rtw88" OR eventMessage CONTAINS[c] "IO80211" OR eventMessage CONTAINS[c] "Skywalk")' \
    2>&1 | tail -1500 || true
} > "$OUT"

echo "Created: $(pwd)/$OUT"
