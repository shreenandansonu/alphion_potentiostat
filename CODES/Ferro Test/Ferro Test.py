
import serial
import time
import csv
import os
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation

from datetime import datetime

# =====================================================
# CONFIGURATION
# =====================================================

PORT = "COM21"
BAUD_RATE = 230400

WE_MIDRAIL_V = 1.650

CV_MIN_V = -0.200
CV_MAX_V = 0.600

SCAN_RATE = 150.0
TOTAL_CYCLES = 2

# Peak analysis window
PEAK_MIN_V = -0.10
PEAK_MAX_V = 0.50

# =====================================================
# DATA STORAGE
# =====================================================

data = {
    cycle: {
        1: {"E": [], "I": []},
        -1: {"E": [], "I": []}
    }
    for cycle in range(1, TOTAL_CYCLES + 1)
}

latest = {
    "cycle": 0,
    "direction": 1,
    "potential": 0.0,
    "we_midrail": WE_MIDRAIL_V,
    "dac": 0.0,
    "tia": 0.0,
    "current": 0.0,
    "saturated": 0
}

total_samples = 0
invalid_samples = 0
experiment_complete = False

# =====================================================
# CSV FILE
# =====================================================

timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

filename = f"Ferri_Ferro_CV_{timestamp}.csv"

csv_file = open(
    filename,
    "w",
    newline=""
)

csv_writer = csv.writer(csv_file)

csv_writer.writerow([
    "TIME_US",
    "CYCLE",
    "DIRECTION",
    "E_WE_RE_V",
    "WE_MIDRAIL_V",
    "DAC_SIG_V",
    "TIA_V",
    "CURRENT_uA",
    "SAT"
])

# =====================================================
# SERIAL CONNECTION
# =====================================================

print("=" * 75)
print("       FERRI / FERROCYANIDE CYCLIC VOLTAMMETRY")
print("=" * 75)

print("Serial port:", PORT)
print("Baud rate:", BAUD_RATE)
print("WE midrail:", WE_MIDRAIL_V, "V")
print("CV window:", CV_MIN_V, "to", CV_MAX_V, "V")
print("Scan rate:", SCAN_RATE, "mV/s")
print("Cycles:", TOTAL_CYCLES)

ser = serial.Serial(
    PORT,
    BAUD_RATE,
    timeout=0.05
)

# ESP32 resets when serial port is opened
time.sleep(2.5)

# Do not clear the input buffer here:
# ESP32 may already have completed calibration.

# =====================================================
# WAIT FOR ESP32
# =====================================================

print("\nWaiting for ESP32 calibration...")

ready = False
start_time = time.time()

while time.time() - start_time < 40:

    raw = ser.readline().decode(
        "utf-8",
        errors="ignore"
    ).strip()

    if not raw:
        continue

    print(raw)

    if raw == "READY_TO_START":
        ready = True
        break

if not ready:

    ser.close()
    csv_file.close()

    raise RuntimeError(
        "ESP32 not ready. Check the hardware and serial connection."
    )

# =====================================================
# START EXPERIMENT
# =====================================================

print("\n" + "=" * 75)

input(
    "Ensure WE, RE and CE are connected correctly. "
    "Press ENTER to start CV..."
)

ser.write(b"s")

print("\nCV experiment started.")

# =====================================================
# GRAPH SETUP
# =====================================================

plt.ion()

fig, ax = plt.subplots(figsize=(13, 8))

fig.patch.set_facecolor("white")
ax.set_facecolor("white")

ax.set_xlim(
    CV_MIN_V - 0.05,
    CV_MAX_V + 0.05
)

ax.axhline(
    0,
    color="gray",
    linestyle=":",
    linewidth=1
)

ax.axvline(
    0,
    color="gray",
    linestyle=":",
    linewidth=1
)

ax.set_xlabel(
    "Electrochemical Potential WE vs RE (V)",
    fontsize=12
)

ax.set_ylabel(
    "Current (uA)",
    fontsize=12
)

ax.set_title(
    "Ferri / Ferrocyanide Cyclic Voltammogram",
    fontsize=15,
    fontweight="bold"
)

ax.grid(
    True,
    linestyle="--",
    alpha=0.4
)

# =====================================================
# LIVE CURVES
# =====================================================

lines = {}

for cycle in range(1, TOTAL_CYCLES + 1):

    lines[(cycle, 1)], = ax.plot(
        [],
        [],
        linewidth=1.5,
        label=f"Cycle {cycle} Forward"
    )

    lines[(cycle, -1)], = ax.plot(
        [],
        [],
        linewidth=1.5,
        linestyle="--",
        label=f"Cycle {cycle} Reverse"
    )

# Peak markers
ipa_marker, = ax.plot(
    [],
    [],
    marker="^",
    color="black",
    markersize=9,
    linestyle="None",
    label="Anodic peak"
)

ipc_marker, = ax.plot(
    [],
    [],
    marker="v",
    color="red",
    markersize=9,
    linestyle="None",
    label="Cathodic peak"
)

ax.legend(loc="best")

# =====================================================
# LIVE INFORMATION
# =====================================================

info_text = fig.text(
    0.5,
    0.015,
    "Waiting for measurements...",
    ha="center",
    fontsize=10,
    fontweight="bold"
)

midrail_text = fig.text(
    0.5,
    0.045,
    "WE virtual ground = 1.650 V",
    ha="center",
    fontsize=10,
    color="darkgreen"
)

fig.tight_layout(
    rect=[0, 0.10, 1, 1]
)

# =====================================================
# PEAK CALCULATION
# =====================================================

def calculate_peaks(cycle):

    forward = data[cycle][1]
    reverse = data[cycle][-1]

    if len(forward["E"]) < 5:
        return None

    if len(reverse["E"]) < 5:
        return None

    Ef = np.asarray(forward["E"])
    If = np.asarray(forward["I"])

    Er = np.asarray(reverse["E"])
    Ir = np.asarray(reverse["I"])

    mask_f = (
        (Ef >= PEAK_MIN_V) &
        (Ef <= PEAK_MAX_V)
    )

    mask_r = (
        (Er >= PEAK_MIN_V) &
        (Er <= PEAK_MAX_V)
    )

    if not np.any(mask_f) or not np.any(mask_r):
        return None

    f_indices = np.where(mask_f)[0]
    r_indices = np.where(mask_r)[0]

    f_idx = f_indices[np.argmax(If[mask_f])]
    r_idx = r_indices[np.argmin(Ir[mask_r])]

    ipa = If[f_idx]
    epa = Ef[f_idx]

    ipc = Ir[r_idx]
    epc = Er[r_idx]

    delta_ep = abs(epa - epc)

    ratio = (
        abs(ipa / ipc)
        if abs(ipc) > 0.001
        else np.nan
    )

    return {
        "ipa": ipa,
        "epa": epa,
        "ipc": ipc,
        "epc": epc,
        "delta_ep": delta_ep,
        "ratio": ratio
    }

# =====================================================
# SERIAL DATA READER
# =====================================================

def read_serial():

    global total_samples
    global invalid_samples
    global experiment_complete

    while ser.in_waiting:

        raw = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not raw:
            continue

        if raw.startswith("#"):

            print(raw)

            if raw == "# CV_COMPLETE":
                experiment_complete = True

            continue

        if raw.startswith("TIME_US"):
            continue

        if raw == "READY_TO_START":
            continue

        try:

            values = raw.split(",")

            if len(values) != 9:

                invalid_samples += 1
                print("Invalid CSV:", raw)

                continue

            t = int(values[0])
            cycle = int(values[1])
            direction = int(values[2])

            potential = float(values[3])
            we_midrail = float(values[4])
            dac_voltage = float(values[5])
            tia_voltage = float(values[6])

            current = float(values[7])
            saturated = int(values[8])

            if cycle not in data:
                continue

            if direction not in (1, -1):
                continue

            # Store measurements
            data[cycle][direction]["E"].append(
                potential
            )

            data[cycle][direction]["I"].append(
                current
            )

            total_samples += 1

            # Save raw CSV
            csv_writer.writerow(values)

            # Flush periodically
            if total_samples % 20 == 0:
                csv_file.flush()

            # Latest values
            latest["cycle"] = cycle
            latest["direction"] = direction
            latest["potential"] = potential
            latest["we_midrail"] = we_midrail
            latest["dac"] = dac_voltage
            latest["tia"] = tia_voltage
            latest["current"] = current
            latest["saturated"] = saturated

            if total_samples % 10 == 0:

                direction_text = (
                    "FORWARD"
                    if direction == 1
                    else "REVERSE"
                )

                print(
                    f"Cycle {cycle} | "
                    f"{direction_text:7s} | "
                    f"WE={we_midrail:.3f} V | "
                    f"E={potential:+.4f} V | "
                    f"I={current:+.3f} uA"
                    + (
                        " | SATURATION"
                        if saturated
                        else ""
                    )
                )

        except (ValueError, IndexError) as error:

            invalid_samples += 1

            print("Parsing error:", error)
            print("Received:", raw)

# =====================================================
# GRAPH UPDATE
# =====================================================

def update(frame):

    read_serial()

    if total_samples == 0:
        return

    # Update CV curves
    for cycle in range(1, TOTAL_CYCLES + 1):

        for direction in (1, -1):

            E = np.asarray(
                data[cycle][direction]["E"]
            )

            I = np.asarray(
                data[cycle][direction]["I"]
            )

            lines[(cycle, direction)].set_data(
                E,
                I
            )

    # Dynamic Y axis
    all_currents = []

    for cycle in data:

        for direction in data[cycle]:

            all_currents.extend(
                data[cycle][direction]["I"]
            )

    if all_currents:

        ymin = min(all_currents)
        ymax = max(all_currents)

        span = max(ymax - ymin, 0.1)

        ax.set_ylim(
            ymin - 0.15 * span,
            ymax + 0.15 * span
        )

    # Peak analysis
    cycle = latest["cycle"]

    peak = calculate_peaks(cycle)

    if peak is not None:

        ipa_marker.set_data(
            [peak["epa"]],
            [peak["ipa"]]
        )

        ipc_marker.set_data(
            [peak["epc"]],
            [peak["ipc"]]
        )

        peak_text = (
            f"Ipa = {peak['ipa']:.3f} uA | "
            f"Epa = {peak['epa']:.4f} V\n"
            f"Ipc = {peak['ipc']:.3f} uA | "
            f"Epc = {peak['epc']:.4f} V | "
            f"Delta Ep = {peak['delta_ep']*1000:.1f} mV | "
            f"|Ipa/Ipc| = {peak['ratio']:.3f}"
        )

    else:

        peak_text = "Peak analysis: collecting data..."

    direction_text = (
        "FORWARD"
        if latest["direction"] == 1
        else "REVERSE"
    )

    saturation_text = (
        " | ADC SATURATED"
        if latest["saturated"]
        else ""
    )

    info_text.set_text(
        f"Cycle: {cycle} | "
        f"Direction: {direction_text} | "
        f"WE: {latest['we_midrail']:.3f} V | "
        f"E: {latest['potential']:+.4f} V | "
        f"I: {latest['current']:+.3f} uA"
        f"{saturation_text}\n"
        f"{peak_text}"
    )

    return list(lines.values()) + [
        ipa_marker,
        ipc_marker,
        info_text
    ]

# =====================================================
# START LIVE PLOT
# =====================================================

ani = animation.FuncAnimation(
    fig,
    update,
    interval=50,
    blit=False,
    cache_frame_data=False
)

try:

    plt.show(block=True)

finally:

    ser.close()

    csv_file.flush()
    csv_file.close()

    print("\n" + "=" * 75)
    print("EXPERIMENT FINISHED")
    print("=" * 75)

    print("Total samples:", total_samples)
    print("Invalid samples:", invalid_samples)

    print("Experiment complete:", experiment_complete)

    print(
        "CSV saved:",
        os.path.abspath(filename)
    )
