#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: apply_exp11_tx_batch.py <rtw88-stable-root>')
root = Path(sys.argv[1])
p = root / 'drivers/net/wireless/realtek/rtw88/tx.c'
s = p.read_text()
marker = 'exp11 macOS safe A-MPDU doorbell batching'
if marker in s:
    print('exp11 TX batching edits already applied.')
    raise SystemExit(0)

# Locate rtw_tx() structurally instead of depending on an exact upstream prologue.
needle = 'void rtw_tx('
start = s.find(needle)
if start < 0:
    raise SystemExit('ERROR: exp11: rtw_tx() not found')
brace = s.find('{', start)
if brace < 0:
    raise SystemExit('ERROR: exp11: rtw_tx() opening brace not found')
depth = 0
end = None
for i in range(brace, len(s)):
    c = s[i]
    if c == '{': depth += 1
    elif c == '}':
        depth -= 1
        if depth == 0:
            end = i + 1
            break
if end is None:
    raise SystemExit('ERROR: exp11: rtw_tx() closing brace not found')
fn = s[start:end]

# Add a local defer flag after pkt_info declaration. exp8 has already run.
decl = 'struct rtw_tx_pkt_info pkt_info = {0};'
pos = fn.find(decl)
if pos < 0:
    raise SystemExit('ERROR: exp11: pkt_info declaration not found in rtw_tx()')
insert_at = pos + len(decl)
fn = fn[:insert_at] + '''\n#ifdef RTW88_MACOS\n\tbool macos_defer_kick = false;\n#endif''' + fn[insert_at:]

# Decide after pkt_info is fully populated but before the HCI write mutates skb.
anchor = '\trtw88_exp7_tx_diag_record(&pkt_info);'
pos = fn.find(anchor)
if pos < 0:
    raise SystemExit('ERROR: exp11: exp8 TX diagnostic anchor not found')
insert_at = pos + len(anchor)
fn = fn[:insert_at] + '''\n#ifdef RTW88_MACOS\n\t/* exp11 macOS safe A-MPDU doorbell batching: only defer data that the\n\t * rtw88 packet-info path has already marked aggregation-eligible.\n\t * Management/EAPOL/pre-BA traffic remains immediate. The kext bounds\n\t * the defer window to 4 frames or 250 us and flushes small packets. */\n\tmacos_defer_kick = pkt_info.ampdu_en;\n#endif''' + fn[insert_at:]

kick = '\trtw_hci_tx_kick_off(rtwdev);'
pos = fn.find(kick)
if pos < 0:
    raise SystemExit('ERROR: exp11: rtw_hci_tx_kick_off() not found in rtw_tx()')
replacement = '''#ifdef RTW88_MACOS\n\tif (!macos_defer_kick)\n\t\trtw_hci_tx_kick_off(rtwdev);\n#else\n\trtw_hci_tx_kick_off(rtwdev);\n#endif'''
fn = fn[:pos] + replacement + fn[pos+len(kick):]

# Replace function and export a tiny explicit kick helper for the kext timer/count path.
s = s[:start] + fn + s[end:]
# Find end again in modified source by locating fn text.
new_end = start + len(fn)
helper = '''\n\n#ifdef RTW88_MACOS\nvoid rtw88_macos_tx_kick(struct rtw_dev *rtwdev)\n{\n\tif (rtwdev)\n\t\trtw_hci_tx_kick_off(rtwdev);\n}\n#endif\n'''
s = s[:new_end] + helper + s[new_end:]
p.write_text(s)
print('exp11 safe TX doorbell batching edits ready.')
