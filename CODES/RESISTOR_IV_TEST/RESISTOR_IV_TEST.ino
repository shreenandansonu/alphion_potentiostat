
import serial
import time
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

# ==========================================
# SERIAL SETTINGS
# ==========================================

PORT = "COM5"
BAUD_RATE = 230400

V_OFFSET = 1.65

# Expected test resistor
EXPECTED_RESISTANCE = 10000.0

# ==========================================
# DATA STORAGE
# ==========================================

voltage_data = deque(maxlen=5000)
current_data = deque(maxlen=5000)

# ==========================================
# SERIAL CONNECTION
# ==========================================

ser = serial.Serial(PORT, BAUD_RATE, timeout=0.02)
time.sleep(2)
ser.reset_input_buffer()

print("Connected to potentiostat")

# ==========================================
# GRAPH SETUP
# ==========================================

fig, ax = plt.subplots(figsize=(11, 7))

fig.patch.set_facecolor("white")
ax.set_facecolor("white")

iv_line, = ax.plot(
    [], [],
    color="royalblue",
    linewidth=1.5,
    marker=".",
    markersize=3,
    label="Measured I-V curve"
)

fit_line, = ax.plot(
    [], [],
    color="red",
    linestyle="--",
    linewidth=2,
    label="Linear fit"
)

ax.axhline(0, color="gray", linestyle=":", linewidth=1)
ax.axvline(0, color="gray", linestyle=":", linewidth=1)

ax.set_title(
    "Potentiostat - 10 kOhm Resistor I-V Characteristic",
    fontsize=15,
    fontweight="bold",
    color="black"
)

ax.set_xlabel("Applied Voltage (V)", color="black")
ax.set_ylabel("Current (uA)", color="black")

ax.set_xlim(-0.22, 0.22)
ax.set_ylim(-25, 25)

ax.grid(True, linestyle="--", color="lightgray")
ax.tick_params(colors="black")

for spine in ax.spines.values():
    spine.set_color("black")

ax.legend(loc="upper left")

# Resistance information
info = ax.text(
    0.98, 0.05,
    "Resistance: Waiting for data",
    transform=ax.transAxes,
    ha="right",
    fontsize=11,
    color="darkgreen",
    bbox=dict(facecolor="white", edgecolor="gray")
)

fig.tight_layout()

# ==========================================
# SERIAL DATA READER
# ==========================================

def read_serial():

    while ser.in_waiting:

        raw = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not raw or raw.startswith("TIME_MS"):
            continue

        if raw.startswith("ERROR"):
            print(raw)
            continue

        try:
            values = raw.split(",")

            if len(values) != 6:
                continue

            dac_voltage = float(values[2])
            current_uA = float(values[4])

            # Applied voltage relative to offset
            applied_voltage = dac_voltage - V_OFFSET

            voltage_data.append(applied_voltage)
            current_data.append(current_uA)

        except ValueError:
            continue

# ==========================================
# UPDATE I-V GRAPH
# ==========================================

def update(frame):

    read_serial()

    if len(voltage_data) < 3:
        return iv_line, fit_line, info

    x = np.array(voltage_data)
    y = np.array(current_data)

    # Plot measured points
    iv_line.set_data(x, y)

    # Linear regression: I = slope*V + intercept
    slope, intercept = np.polyfit(x, y, 1)

    fit_x = np.linspace(-0.20, 0.20, 100)
    fit_y = slope * fit_x + intercept

    fit_line.set_data(fit_x, fit_y)

    # Convert uA/V to ohms
    if abs(slope) > 0.001:
        measured_resistance = 1e6 / abs(slope)

        info.set_text(
            f"Measured R: {measured_resistance:.1f} Ohm\n"
            f"Expected R: {EXPECTED_RESISTANCE:.0f} Ohm\n"
            f"Slope: {slope:.2f} uA/V"
        )

    return iv_line, fit_line, info

# ==========================================
# START
# ==========================================

ani = animation.FuncAnimation(
    fig,
    update,
    interval=50,
    blit=False,
    cache_frame_data=False
)

try:
    plt.show()

finally:
    ser.close()
    print("Serial connection closed")
