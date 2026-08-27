#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1])
phy = root / "drivers/net/wireless/realtek/rtw88/phy.c"
fw = root / "drivers/net/wireless/realtek/rtw88/fw.c"
main_h = root / "drivers/net/wireless/realtek/rtw88/main.h"

def replace(path, old, new):
    text = path.read_text()
    if old not in text:
        raise SystemExit(f"exp23 anchor missing in {path}")
    path.write_text(text.replace(old, new, 1))

replace(main_h,
"\tu8 rssi_level;\n",
"\tu8 rssi_level;\n"
"#ifdef RTW88_MACOS\n"
"\tu8 macos_ra_raw_rssi;\n"
"\tu8 macos_ra_calibrated_rssi;\n"
"\tu8 macos_ra_fw_rssi;\n"
"#endif\n")

old = """\trssi = ewma_rssi_read(&si->avg_rssi);
\tsi->rssi_level = rtw_phy_get_rssi_level(si->rssi_level, rssi);

\trtw_fw_send_rssi_info(rtwdev, si);
"""
new = """\trssi = ewma_rssi_read(&si->avg_rssi);
#ifdef RTW88_MACOS
\t/* exp23: the corrected EWMA is deliberately preserved.  Calibrate only
\t * the strong-signal RA input; exp12's broken EWMA accidentally provided
\t * a similar positive bias, while weak links remain untouched. */
\tsi->macos_ra_raw_rssi = rssi;
\tif (rssi >= 46)
\t\tsi->macos_ra_calibrated_rssi = min_t(u8, rssi + 18, 100);
\telse if (rssi >= 38)
\t\tsi->macos_ra_calibrated_rssi = min_t(u8, rssi + 8, 100);
\telse
\t\tsi->macos_ra_calibrated_rssi = rssi;
\trssi = si->macos_ra_calibrated_rssi;
#endif
\tsi->rssi_level = rtw_phy_get_rssi_level(si->rssi_level, rssi);

\trtw_fw_send_rssi_info(rtwdev, si);
"""
replace(phy, old, new)

old = "\tu8 rssi = ewma_rssi_read(&si->avg_rssi);\n"
new = """\tu8 rssi = ewma_rssi_read(&si->avg_rssi);
#ifdef RTW88_MACOS
\t/* exp23: use the value calibrated by the watchdog RSSI pass. */
\tif (si->macos_ra_calibrated_rssi)
\t\trssi = si->macos_ra_calibrated_rssi;
\tsi->macos_ra_fw_rssi = rssi;
#endif
"""
replace(fw, old, new)

print("Applied exp23 strong-signal RA calibration")
