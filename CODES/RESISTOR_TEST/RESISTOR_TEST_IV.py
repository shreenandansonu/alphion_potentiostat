
import serial
import time
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

# ==========================================
# SERIAL SETTINGS
# ==========================================

PORT = "COM21"
BAUD_RATE = 230400

V_OFFSET = 1.65
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

fig, ax = plt.subplots(figsize=(11, 8))

fig.patch.set_facecolor("white")
ax.set_facecolor("white")

# Only measured I-V points: no red fit line
iv_line, = ax.plot(
    [], [],
    color="royalblue",
    linewidth=1.5,
    marker=".",
    markersize=3,
    label="Measured I-V curve"
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

# ==========================================
# LIVE RESISTANCE DISPLAY BELOW GRAPH
# ==========================================

resistance_text = fig.text(
    0.5,
    0.035,
    "Resistance: Waiting for data",
    ha="center",
    fontsize=13,
    fontweight="bold",
    color="darkgreen"
)

fig.tight_layout(rect=[0, 0.09, 1, 1])

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

            applied_voltage = dac_voltage - V_OFFSET

            voltage_data.append(applied_voltage)
            current_data.append(current_uA)

        except ValueError:
            continue

# ==========================================
# GRAPH UPDATE
# ==========================================

def update(frame):

    read_serial()

    if not voltage_data:
        return iv_line, resistance_text

    x = np.array(voltage_data)
    y = np.array(current_data)

    # Update measured I-V curve
    iv_line.set_data(x, y)

    # Calculate resistance at latest valid point
    voltage = x[-1]
    current = y[-1]

    if abs(current) >= 0.1 and abs(voltage) >= 0.002:

        resistance = abs(voltage / (current / 1e6))

        resistance_text.set_text(
            f"Current Point: V = {voltage:+.4f} V   |   "
            f"I = {current:+.3f} uA   |   "
            f"R = {resistance:.1f} Ohm"
        )

    else:
        resistance_text.set_text(
            f"Current Point: V = {voltage:+.4f} V   |   "
            f"I = {current:+.3f} uA   |   R = N/A "
            "(near zero crossing)"
        )

    return iv_line, resistance_text

# ==========================================
# START LIVE PLOT
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
