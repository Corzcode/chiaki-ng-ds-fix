#!/usr/bin/env python3
"""Replay captured DualSense HID reports through chiaki's battery parser.

Feeds the raw input reports recorded by chiaki's DualSense battery debug log
back through the same rules implemented in
gui/src/controllermanager.cpp :: UpdateDualSenseBatteryFromHID().

Use this to re-verify the status0 byte offsets after a controller firmware
update, when adding a new DualSense variant, or whenever battery readings look
implausible again.

Usage:  python scripts/analyze_ds_battery_log.py [path/to/dualsense_debug.txt]
"""

import re
import sys
from collections import Counter, defaultdict

# ---- mirrors the #defines in gui/src/controllermanager.cpp -------------------
REPORT_ID_USB = 0x01
REPORT_ID_BT = 0x31
STATUS0_OFFSET_USB = 53
STATUS0_OFFSET_BT = 54
BATTERY_LEVEL_MAX = 10
BATTERY_PERCENT_STEP = 10

CHARGE_DISCHARGING = 0x0
CHARGE_CHARGING = 0x1
CHARGE_COMPLETE = 0x2
CHARGE_VOLTAGE_ERR = 0xA
CHARGE_TEMP_ERR = 0xB
CHARGE_ERROR = 0xF

STATUS_NAMES = {
    CHARGE_DISCHARGING: "Discharging",
    CHARGE_CHARGING: "Charging",
    CHARGE_COMPLETE: "Full",
    CHARGE_VOLTAGE_ERR: "Unknown (voltage error)",
    CHARGE_TEMP_ERR: "Unknown (temperature error)",
    CHARGE_ERROR: "Unknown (charging error)",
}

TRANSPORT = {REPORT_ID_USB: "USB", REPORT_ID_BT: "Bluetooth"}


def load_reports(path):
    """Yield raw report byte lists from a chiaki DualSense debug log."""
    reports, pending = [], False
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if re.match(r"res=\d+ status0=0x[0-9a-f]{2}", line):
                pending = True
                continue
            if line == "---" or line.startswith("HID OPEN"):
                pending = False
                continue
            tokens = line.split()
            if pending and len(tokens) >= 20:
                try:
                    reports.append([int(t, 16) for t in tokens])
                except ValueError:
                    pass
                pending = False
    return reports


def parse(report):
    """Return (percent, state_name) or None - mirrors the C++ logic."""
    if not report:
        return None
    if report[0] == REPORT_ID_USB:
        offset = STATUS0_OFFSET_USB
    elif report[0] == REPORT_ID_BT:
        offset = STATUS0_OFFSET_BT
    else:
        return None                      # unknown report id -> keep SDL value
    if len(report) <= offset:
        return None

    status0 = report[offset]
    level = status0 & 0x0F
    charge = (status0 >> 4) & 0x0F

    if level > BATTERY_LEVEL_MAX:        # implausible -> ignore
        return None

    state = STATUS_NAMES.get(charge, "Unknown")
    percent = level * BATTERY_PERCENT_STEP
    if charge == CHARGE_COMPLETE or level == BATTERY_LEVEL_MAX:
        percent = 100
    return percent, state


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r"E:\test\chiaki-ng\dualsense_debug.txt"
    reports = load_reports(path)
    if not reports:
        print("no reports found in", path)
        return 1

    print(f"replaying {len(reports)} reports from {path}\n")

    by_transport = defaultdict(Counter)
    rejected = Counter()
    timeline = []

    for r in reports:
        result = parse(r)
        if result is None:
            rejected[r[0]] += 1
            continue
        percent, state = result
        by_transport[TRANSPORT[r[0]]][(percent, state)] += 1
        if not timeline or timeline[-1] != (TRANSPORT[r[0]], percent, state):
            timeline.append((TRANSPORT[r[0]], percent, state))

    for transport, counter in by_transport.items():
        total = sum(counter.values())
        print(f"=== {transport} ({total} reports) ===")
        for (percent, state), n in counter.most_common():
            print(f"    {percent:3d}%  {state:<26} {n:5d} reports "
                  f"({n / total * 100:5.1f}%)")
        print()

    if rejected:
        print("=== rejected (implausible level / unknown report id) ===")
        for rid, n in rejected.items():
            print(f"    report id 0x{rid:02x}: {n}")
        print()

    print("=== battery timeline (changes only) ===")
    for transport, percent, state in timeline:
        print(f"    {transport:<10} {percent:3d}%  {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
