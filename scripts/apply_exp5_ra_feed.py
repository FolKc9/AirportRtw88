#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    print('usage: apply_exp5_ra_feed.py <rtw88-stable-root>', file=sys.stderr)
    sys.exit(2)

root = Path(sys.argv[1]).resolve()
main_c = root / 'drivers/net/wireless/realtek/rtw88/main.c'
phy_c = root / 'drivers/net/wireless/realtek/rtw88/phy.c'
phy_h = root / 'drivers/net/wireless/realtek/rtw88/phy.h'

for p in (main_c, phy_c, phy_h):
    if not p.is_file():
        print(f'ERROR: missing upstream file: {p}', file=sys.stderr)
        sys.exit(1)

def replace_once(path: Path, old: str, new: str, already: str, label: str):
    text = path.read_text()
    if already in text:
        print(f'{label}: already applied')
        return
    if old not in text:
        print(f'ERROR: {label}: expected upstream context not found in {path}', file=sys.stderr)
        sys.exit(1)
    text = text.replace(old, new, 1)
    path.write_text(text)
    print(f'{label}: applied')

# Keep the full dynamic RF watchdog disabled, but feed RSSI to firmware RA.
old_main = '''\t/* Diagnostic: skip all RF-dynamic work + coex queries. */\n\tif (rtw88_disable_watchdog_work)\n\t\tgoto unlock;\n'''
new_main = '''\t/* macOS: keep the potentially troublesome full RF-dynamic path disabled,\n\t * but retain the one piece firmware rate adaptation needs: fresh RSSI for\n\t * the associated STA.  This deliberately avoids DIG/DPK/power tracking. */\n\tif (rtw88_disable_watchdog_work) {\n\t\trtw_phy_macos_rssi_feed(rtwdev);\n\t\trtwdev->watch_dog_cnt++;\n\t\tgoto unlock;\n\t}\n'''
replace_once(main_c, old_main, new_main,
             'rtw_phy_macos_rssi_feed(rtwdev);', 'main.c minimal watchdog RA feed')

# Expose a tiny wrapper around the existing station-RSSI statistics path.
old_phy = '''static void rtw_phy_stat_rate_cnt(struct rtw_dev *rtwdev)\n{\n'''
new_phy = '''/* Feixiao/macOS minimal watchdog path: feed the firmware's rate-adaptation\n * logic with per-station RSSI while leaving the rest of the dynamic PHY\n * mechanism disabled. */\nvoid rtw_phy_macos_rssi_feed(struct rtw_dev *rtwdev)\n{\n\trtw_phy_stat_rssi(rtwdev);\n}\n\nstatic void rtw_phy_stat_rate_cnt(struct rtw_dev *rtwdev)\n{\n'''
replace_once(phy_c, old_phy, new_phy,
             'void rtw_phy_macos_rssi_feed(struct rtw_dev *rtwdev)',
             'phy.c RA RSSI wrapper')

old_h = '''void rtw_phy_dynamic_mechanism(struct rtw_dev *rtwdev);\n'''
new_h = '''void rtw_phy_dynamic_mechanism(struct rtw_dev *rtwdev);\nvoid rtw_phy_macos_rssi_feed(struct rtw_dev *rtwdev);\n'''
replace_once(phy_h, old_h, new_h,
             'void rtw_phy_macos_rssi_feed(struct rtw_dev *rtwdev);',
             'phy.h RA RSSI declaration')

print('exp5 RA feed source edits complete.')
