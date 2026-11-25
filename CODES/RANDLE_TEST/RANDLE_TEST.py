
import serial
import time
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

# =====================================================
# CONFIGURATION (keep in sync with the firmware)
# =====================================================

PORT = "COM21"
BAUD_RATE = 230400

V_OFFSET = 1.65

CV_MIN_V = -0.50
CV_MAX_V = 0.50

EXPECTED_SCAN_RATE = 100.0          # mV/s
R_TOTAL = 101_000.0                 # Rs + Rct, ohms
EXPECTED_SLOPE = 1e6 / R_TOTAL      # uA/V

PRINT_RAW_DATA = True
MAX_POINTS = 20000

# =====================================================
# DATA STORAGE
# =====================================================

voltage_data = deque(maxlen=MAX_POINTS)
current_data = deque(maxlen=MAX_POINTS)
tia_data = deque(maxlen=MAX_POINTS)
dac_data = deque(maxlen=MAX_POINTS)
time_data = deque(maxlen=MAX_POINTS)

current_cycle = None
current_direction = 1

cycle_start_time = None
previous_timestamp = None
previous_voltage = None

latest_dac = 0.0
latest_applied = 0.0
latest_tia = 0.0
latest_current = 0.0
latest_scan_rate = 0.0
latest_saturated = 0

total_samples = 0
invalid_samples = 0

# =====================================================
# SERIAL CONNECTION
# =====================================================

ser = serial.Serial(PORT, BAUD_RATE, timeout=0.05)

time.sleep(2)
ser.reset_input_buffer()

print("\n")
print("=" * 100)
print("             RANDLES CIRCUIT - CV DIAGNOSTICS")
print("=" * 100)
print(f"Serial Port       : {PORT}")
print(f"Baud Rate         : {BAUD_RATE}")
print(f"Voltage Offset    : {V_OFFSET:.3f} V")
print(f"Window            : {CV_MIN_V:+.2f} V to {CV_MAX_V:+.2f} V")
print(f"Target Scan Rate  : {EXPECTED_SCAN_RATE:.1f} mV/s")
print(f"Expected slope    : {EXPECTED_SLOPE:.2f} uA/V")
print("=" * 100)

print(
    f"{'TIME(ms)':>10} | "
    f"{'DAC(V)':>9} | "
    f"{'APPLIED(V)':>11} | "
    f"{'TIA(V)':>9} | "
    f"{'CURRENT(uA)':>12} | "
    f"{'SCAN(mV/s)':>11} | "
    f"{'DIRECTION':>8}"
)

print("-" * 100)

# =====================================================
# GRAPH SETUP
# =====================================================

fig, ax = plt.subplots(figsize=(12, 8))
fig.patch.set_facecolor("white")
ax.set_facecolor("white")

cv_line, = ax.plot(
    [], [],
    color="royalblue",
    linewidth=1.5,
    label="Measured CV"
)

current_point, = ax.plot(
    [], [], marker="o", color="black", markersize=7,
    linestyle="None", label="Latest measurement"
)

ideal_line, = ax.plot(
    [CV_MIN_V, CV_MAX_V],
    [CV_MIN_V * EXPECTED_SLOPE, CV_MAX_V * EXPECTED_SLOPE],
    color="red", linestyle="--", linewidth=1,
    label=f"Ideal ({EXPECTED_SLOPE:.1f} uA/V)"
)

ax.axhline(0, color="gray", linestyle=":", linewidth=1)
ax.axvline(0, color="gray", linestyle=":", linewidth=1)

ax.set_title(
    f"Randles Circuit - Continuous CV Diagnostics "
    f"({CV_MIN_V:+.1f} to {CV_MAX_V:+.1f} V, {EXPECTED_SCAN_RATE:.0f} mV/s)",
    fontsize=15, fontweight="bold"
)

ax.set_xlabel("Applied Potential (V)", fontsize=12)
ax.set_ylabel("Current (uA)", fontsize=12)

pad = 0.1 * (CV_MAX_V - CV_MIN_V)
ax.set_xlim(CV_MIN_V - pad, CV_MAX_V + pad)

ax.grid(True, linestyle="--", color="lightgray")
ax.legend(loc="upper left")
ax.tick_params(colors="black")

for spine in ax.spines.values():
    spine.set_color("black")

# =====================================================
# LIVE INFORMATION
# =====================================================

info_text = fig.text(
    0.5, 0.015, "Waiting for measurements...",
    ha="center", fontsize=10, fontweight="bold", color="darkgreen"
)

fig.tight_layout(rect=[0, 0.09, 1, 1])

# =====================================================
# SERIAL DATA READER
# =====================================================

def read_serial():

    global current_cycle, current_direction
    global cycle_start_time, previous_timestamp, previous_voltage
    global latest_dac, latest_applied, latest_tia
    global latest_current, latest_scan_rate, latest_saturated
    global total_samples, invalid_samples

    while ser.in_waiting:

        raw = ser.readline().decode(
            "utf-8", errors="ignore"
        ).strip()

        if not raw:
            continue

        if raw.startswith("TIME"):
            continue

        if raw.startswith("#"):
            print(raw)
            continue

        if raw.startswith("ERROR"):
            print("\nESP32 ERROR:", raw)
            continue

        try:
            values = raw.split(",")

            if len(values) != 9:
                invalid_samples += 1
                print("\nINVALID CSV:", raw)
                continue

            timestamp_us = int(values[0])
            cycle = int(values[1])
            direction = int(values[2])
            dac_code = int(values[3])
            dac_voltage = float(values[4])
            tia_voltage = float(values[5])
            current_uA = float(values[6])

            # values[7] = estimated resistance
            saturated = int(values[8])

            timestamp_ms = timestamp_us / 1000.0
            applied_voltage = dac_voltage - V_OFFSET

            # ---------------- CYCLE DETECTION ----------------

            if current_cycle != cycle:

                if cycle_start_time is not None:
                    duration = (
                        timestamp_ms - cycle_start_time
                    ) / 1000.0

                    print("\n" + "-" * 65)
                    print(
                        f"PREVIOUS CYCLE DURATION: {duration:.2f} seconds"
                    )
                    print("-" * 65)

                current_cycle = cycle
                cycle_start_time = timestamp_ms

                # IMPORTANT:
                # Do not clear the data buffers.
                # Previous CV cycles remain on the graph.

                print("\n" + "=" * 65)
                print(f"STARTING CV CYCLE: {cycle}")
                print("Previous cycle data retained.")
                print("=" * 65)

            # ---------------- SCAN RATE ----------------

            actual_scan_rate = 0.0

            if previous_timestamp is not None:
                dt = (
                    timestamp_ms - previous_timestamp
                ) / 1000.0

                dv = abs(applied_voltage - previous_voltage)

                if dt > 0:
                    actual_scan_rate = (dv / dt) * 1000.0

            previous_timestamp = timestamp_ms
            previous_voltage = applied_voltage
            latest_scan_rate = actual_scan_rate

            # ---------------- STORE ----------------

            voltage_data.append(applied_voltage)
            current_data.append(current_uA)
            tia_data.append(tia_voltage)
            dac_data.append(dac_voltage)
            time_data.append(timestamp_ms)

            total_samples += 1

            latest_dac = dac_voltage
            latest_applied = applied_voltage
            latest_tia = tia_voltage
            latest_current = current_uA
            latest_saturated = saturated
            current_direction = direction

            direction_text = "FORWARD" if direction == 1 else "REVERSE"

            if PRINT_RAW_DATA:
                print(
                    f"{timestamp_ms:10.1f} | "
                    f"{dac_voltage:+9.5f} | "
                    f"{applied_voltage:+11.5f} | "
                    f"{tia_voltage:+9.5f} | "
                    f"{current_uA:+12.3f} | "
                    f"{actual_scan_rate:11.2f} | "
                    f"{direction_text:>8}"
                    + ("  SATURATED" if saturated else ""),
                    flush=True
                )

        except (ValueError, IndexError) as error:
            invalid_samples += 1
            print("\nPARSING ERROR:", error)
            print("RECEIVED:", raw)

# =====================================================
# GRAPH UPDATE
# =====================================================

def update(frame):

    read_serial()

    if not voltage_data:
        return cv_line, current_point, info_text

    x = np.asarray(voltage_data)
    y = np.asarray(current_data)

    cv_line.set_data(x, y)
    current_point.set_data([x[-1]], [y[-1]])

    # Y-axis: include ideal line
    ideal_max = max(abs(CV_MIN_V), abs(CV_MAX_V)) * EXPECTED_SLOPE

    ymin = min(np.min(y), -ideal_max, 0)
    ymax = max(np.max(y), ideal_max, 0)

    margin = max((ymax - ymin) * 0.15, 0.05)

    ax.set_ylim(ymin - margin, ymax + margin)

    # Live slope fit
    slope_text = "Slope: --"

    if len(x) > 20 and np.ptp(x) > 0.2:
        slope, intercept = np.polyfit(x, y, 1)

        slope_text = (
            f"Slope: {slope:.2f} uA/V (ideal {EXPECTED_SLOPE:.2f}) | "
            f"Offset: {intercept:+.3f} uA"
        )

    direction_text = "FORWARD" if current_direction == 1 else "REVERSE"

    sat_text = "  *** TIA NEAR ADC LIMIT ***" if latest_saturated else ""

    info_text.set_text(
        f"Cycle: {current_cycle} | "
        f"Direction: {direction_text} | "
        f"Applied: {latest_applied:+.4f} V | "
        f"Current: {latest_current:+.3f} uA{sat_text}\n"
        f"DAC: {latest_dac:.4f} V | "
        f"TIA: {latest_tia:.4f} V | "
        f"Scan rate: {latest_scan_rate:.2f} mV/s | "
        f"{slope_text}"
    )

    return cv_line, current_point, info_text

# =====================================================
# START LIVE PLOT
# =====================================================

ani = animation.FuncAnimation(
    fig, update, interval=50, blit=False, cache_frame_data=False
)

try:
    plt.show()

finally:
    ser.close()

    print("\n" + "=" * 65)
    print("EXPERIMENT FINISHED")
    print("=" * 65)
    print("Total valid samples:", total_samples)
    print("Invalid samples:", invalid_samples)
    print("Serial connection closed.")
