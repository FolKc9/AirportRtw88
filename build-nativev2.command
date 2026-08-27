#!/bin/bash
set -Eeuo pipefail
cd "$(dirname "$0")"

DIST="$PWD/dist"
UPSTREAM="$PWD/../rtw88-stable"
mkdir -p "$DIST"
rm -rf "$DIST/rtw88.kext" "$DIST/rtw88ctl" "$DIST/BUILD_FAILED.txt"

fail_note() {
  local rc=$?
  {
    echo "Feixiao NativeV2 exp23 build failed (exit $rc)."
    echo "See the Terminal output immediately above for the failing stage."
    echo "The dist/ directory is intentionally kept even on failure."
  } > "$DIST/BUILD_FAILED.txt"
  echo
  echo "ERROR: exp23 build failed (exit $rc)."
  echo "A marker was written to: $DIST/BUILD_FAILED.txt"
  exit "$rc"
}
trap fail_note ERR

echo "== Feixiao NativeV2 exp23 / strong-signal RA calibration build =="
echo "Project: $PWD"
echo "Output : $DIST"

action_fail() {
  echo "ERROR: $*" >&2
  false
}

command -v xcrun >/dev/null 2>&1 || \
  action_fail "run this build on macOS with Xcode Command Line Tools installed."
command -v git >/dev/null 2>&1 || action_fail "git is required."
command -v python3 >/dev/null 2>&1 || action_fail "python3 is required."

# Deterministic upstream tree.  Older exp packages all used ../rtw88-stable,
# which let source edits from one experiment leak into the next.  exp12 always
# recreates the exact Feixiao branch itself; the user no longer has to rm it.
echo "Preparing clean rtw88-stable (branch: feixiao)..."
rm -rf "$UPSTREAM"
git clone --depth 1 --branch feixiao \
  https://github.com/thegwchr/rtw88-stable.git "$UPSTREAM"

RTW88_PATCH="$PWD/patches/rtw88-exp3-rx-partial-sync.patch"
[ -s "$RTW88_PATCH" ] || action_fail "missing exp3 RX patch: $RTW88_PATCH"
echo "Applying exp3 RX partial-sync patch..."
git -C "$UPSTREAM" apply --check "$RTW88_PATCH"
git -C "$UPSTREAM" apply "$RTW88_PATCH"

echo "Applying exp5 minimal RA/RSSI feed..."
python3 "$PWD/scripts/apply_exp5_ra_feed.py" "$UPSTREAM"

echo "Applying exp8 VHT TX speed fix..."
python3 "$PWD/scripts/apply_exp8_tx_speedfix.py" "$UPSTREAM"

echo "Applying exp12 host-deferred A-MPDU TX foundation..."
python3 "$PWD/scripts/apply_exp11_tx_batch.py" "$UPSTREAM"

echo "Applying exp23 strong-signal RA calibration..."
python3 "$PWD/scripts/apply_exp23_ra_calibration.py" "$UPSTREAM"

# Post-conditions: fail here instead of producing a kext that silently lost a
# performance fix because an upstream anchor changed.
grep -qi "copy only the RX descriptor" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/pci.c" || \
  action_fail "exp3 RX partial-sync post-check failed."
grep -q "rtw_phy_macos_rssi_feed(rtwdev);" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/main.c" || \
  action_fail "exp5 RA/RSSI feed post-check failed."
grep -q "exp8 host-start VHT speed fix" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/tx.c" || \
  action_fail "exp8 VHT TX speed-fix post-check failed."
grep -q "exp11 macOS safe A-MPDU doorbell batching" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/tx.c" || \
  action_fail "exp12 TX-defer foundation post-check failed."

if [ ! -d MacKernelSDK/Headers ]; then
  echo "Installing MacKernelSDK..."
  rm -rf MacKernelSDK
  git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git MacKernelSDK
fi

[ -s firmware/rtw8821c_fw.bin ] || \
  action_fail "bundled RTL8821C firmware is missing."

# exp12 packaging sanity: the first exp11 archive exposed TX batching
# accessors but omitted their backing fields from RTW88PCIDevice. Catch that
# packaging mistake before spending time compiling the full driver.
for member in _txBatchPackets _txBatchKicks _txBatchTimerKicks \
              _txBatchImmediateKicks _txBatchPending _txBatchMaxPending; do
  grep -q "${member}[[:space:]]*=" src/kext/RTW88PCIDevice.hpp || \
    action_fail "exp12 TX batching member missing from RTW88PCIDevice.hpp: $member"
done

grep -q "kRTW88TxBulkBatchTarget = 8" src/kext/RTW88PCIDevice.cpp || \
  action_fail "exp12 bulk TX batching source check failed."
grep -q "kRTW88TxAckDelayUs      = 70" src/kext/RTW88PCIDevice.cpp || \
  action_fail "exp12 ACK TX batching source check failed."
grep -q "napi->coalesce_us = 170" src/compat/rtw88_compat.c || \
  action_fail "exp12 adaptive NAPI source check failed."

# exp23 anti-regression checks: preserve the exp12 datapath and exp22 security.
grep -q "kRTW88TxBulkBatchTarget = 8" src/kext/RTW88PCIDevice.cpp || \
  action_fail "exp22 regression: exp12 TX bulk batching changed."
grep -q "kRTW88TxAckDelayUs      = 70" src/kext/RTW88PCIDevice.cpp || \
  action_fail "exp22 regression: exp12 ACK batching changed."
grep -q "__builtin_ctzl" src/compat/linux/average.h || \
  action_fail "exp22 EWMA fix missing."
grep -q "safeSsid" src/kext/RTW88UserClient.cpp || \
  action_fail "exp22 UserClient hardening missing."
grep -q "_ptkInstalledThisHandshake" src/kext/RTW88IEEE80211.hpp || \
  action_fail "exp22 WPA2 state missing."
grep -q "blocked EAPOL M3 key reinstallation" src/kext/RTW88IEEE80211.cpp || \
  action_fail "exp22 WPA2 key-reinstall protection missing."

echo "exp22 preflight: exp12 datapath preserved + security fixes present"

grep -q "macos_ra_calibrated_rssi" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/phy.c" || \
  action_fail "exp23 RA calibration missing."
grep -q "macos_ra_fw_rssi" \
  "$UPSTREAM/drivers/net/wireless/realtek/rtw88/fw.c" || \
  action_fail "exp23 firmware RSSI feed missing."
grep -q "exp23 RA truth" ctl/main.c || \
  action_fail "exp23 RA telemetry missing."

echo "exp23 preflight: calibrated firmware RSSI + minimal telemetry present"

echo "Building kext + rtw88ctl..."
make clean
make all

[ -d build/out/rtw88.kext ] || \
  action_fail "make completed without build/out/rtw88.kext"
[ -f build/out/rtw88ctl ] || \
  action_fail "make completed without build/out/rtw88ctl"
grep -q "<string>1.1.23</string>" build/out/rtw88.kext/Contents/Info.plist || \
  action_fail "built kext version is not 1.1.23"
grep -q "NativeV2-StrongSignalRA-exp23" build/out/rtw88.kext/Contents/Info.plist || \
  action_fail "built kext flavor is not exp23"

rm -rf "$DIST/rtw88.kext" "$DIST/rtw88ctl"
cp -R build/out/rtw88.kext "$DIST/rtw88.kext"
cp build/out/rtw88ctl "$DIST/rtw88ctl"
chmod +x "$DIST/rtw88ctl"
codesign --force --deep --sign - "$DIST/rtw88.kext" 2>/dev/null || true

[ -d "$DIST/rtw88.kext/Contents" ] || action_fail "dist kext copy verification failed."
[ -x "$DIST/rtw88ctl" ] || action_fail "dist rtw88ctl copy verification failed."
rm -f "$DIST/BUILD_FAILED.txt"

cat > "$DIST/README.txt" <<'TXT'
NativeV2 exp23 build output (exp12 datapath + strong-signal RA calibration)
- rtw88.kext: install this in the EFI in place of the previous NativeV2 kext.
- rtw88ctl: matching diagnostics/control binary.
- Datapath: intentionally exp12 for RX/NAPI/TX batching.
- Added: strong-signal RSSI calibration for firmware RA, with EWMA preserved.
- Security: exp22 WPA2/UserClient hardening preserved.
- Excluded: BQL, A-MSDU, immediate NAPI, forced ASPM-off, later recovery experiments.
Keep a bootable backup EFI before testing experimental kernel code.
TXT

echo
echo "Build complete. dist/ verified:"
ls -lah "$DIST"
echo
echo "  Kext: $DIST/rtw88.kext"
echo "  CLI : $DIST/rtw88ctl"
echo "Version: 1.1.23 / NativeV2-StrongSignalRA-exp23"
