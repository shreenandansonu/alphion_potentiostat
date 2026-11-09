
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import time
import math

# =====================================================
# SERIAL SETTINGS
# =====================================================

PORT = "COM21"             # Change to your ESP32 COM port
BAUD_RATE = 230400

# =====================================================
# GRAPH SETTINGS
# =====================================================

WINDOW_SECONDS = 10
UPDATE_INTERVAL = 50
MAX_POINTS = 10000

# Voltage graph
V_MIN = 1.20
V_MAX = 2.10

# Resistance graph
R_MIN = 0
R_MAX = 20000

# =====================================================
# DATA STORAGE
# =====================================================

timestamps = deque(maxlen=MAX_POINTS)
dac_values = deque(maxlen=MAX_POINTS)
tia_values = deque(maxlen=MAX_POINTS)
current_values = deque(maxlen=MAX_POINTS)
resistance_values = deque(maxlen=MAX_POINTS)

start_mcu_time = None

# =====================================================
# SERIAL CONNECTION
# =====================================================

print("Connecting to potentiostat...")

ser = serial.Serial(
    PORT,
    BAUD_RATE,
    timeout=0.02
)

time.sleep(2)
ser.reset_input_buffer()

print("Connected successfully!")

# =====================================================
# FIGURE
# =====================================================

fig, (ax1, ax2, ax3) = plt.subplots(
    3, 1,
    figsize=(14, 10),
    sharex=True,
    gridspec_kw={
        "height_ratios": [2, 1, 1]
    }
)

fig.patch.set_facecolor("white")

for ax in (ax1, ax2, ax3):

    ax.set_facecolor("white")

    ax.grid(
        True,
        linestyle="--",
        linewidth=0.7,
        color="lightgray",
        alpha=0.8
    )

    ax.tick_params(colors="black")

    for spine in ax.spines.values():
        spine.set_color("black")

# =====================================================
# GRAPH 1: VOLTAGE
# =====================================================

dac_line, = ax1.plot(
    [], [],
    color="royalblue",
    linewidth=2,
    label="DAC Setpoint"
)

tia_line, = ax1.plot(
    [], [],
    color="seagreen",
    linewidth=2,
    label="TIA Output"
)

ax1.axhline(
    1.65,
    color="gray",
    linestyle="--",
    linewidth=1.2,
    label="Nominal Offset"
)

ax1.set_title(
    "10 kOhm Resistor Test - Potentiostat",
    fontsize=16,
    fontweight="bold",
    color="black"
)

ax1.set_ylabel("Voltage (V)", color="black")
ax1.set_ylim(V_MIN, V_MAX)
ax1.legend(loc="upper right")

# =====================================================
# GRAPH 2: CURRENT
# =====================================================

current_line, = ax2.plot(
    [], [],
    color="purple",
    linewidth=1.8,
    label="Measured Current"
)

ax2.axhline(
    0,
    color="gray",
    linestyle="--",
    linewidth=1
)

ax2.set_ylabel("Current (uA)", color="black")
ax2.legend(loc="upper right")

# =====================================================
# GRAPH 3: RESISTANCE
# =====================================================

resistance_line, = ax3.plot(
    [],
    [],
    color="darkorange",
    linewidth=1.8,
    label="Estimated Resistance"
)

ax3.axhline(
    10000,
    color="green",
    linestyle="--",
    linewidth=1.5,
    label="Expected 10 kOhm"
)

ax3.set_xlabel("Time (seconds)", color="black")
ax3.set_ylabel("Resistance (Ohm)", color="black")
ax3.set_ylim(R_MIN, R_MAX)
ax3.legend(loc="upper right")

fig.tight_layout()

# =====================================================
# SERIAL DATA READER
# =====================================================

def read_serial_data():

    global start_mcu_time

    while ser.in_waiting:

        raw = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not raw:
            continue

        if raw.startswith("TIME_MS"):
            continue

        if raw.startswith("ERROR"):
            print(raw)
            continue

        try:

            values = raw.split(",")

            if len(values) != 6:
                continue

            mcu_time = int(values[0])

            dac_voltage = float(values[2])
            tia_voltage = float(values[3])
            current_uA = float(values[4])
            resistance = float(values[5])

            if start_mcu_time is None:
                start_mcu_time = mcu_time

            # ESP32 time in seconds
            t = (mcu_time - start_mcu_time) / 1000.0

            timestamps.append(t)
            dac_values.append(dac_voltage)
            tia_values.append(tia_voltage)
            current_values.append(current_uA)

            # Keep invalid values as NaN.
            # Matplotlib will show gaps instead of spikes.
            if math.isfinite(resistance):
                resistance_values.append(resistance)
            else:
                resistance_values.append(float("nan"))

        except (ValueError, IndexError):
            continue

# =====================================================
# UPDATE GRAPHS
# =====================================================

def update_graph(frame):

    read_serial_data()

    if not timestamps:
        return (
            dac_line,
            tia_line,
            current_line,
            resistance_line
        )

    current_time = timestamps[-1]

    # Remove old data
    while (
        timestamps
        and timestamps[0] < current_time - WINDOW_SECONDS
    ):

        timestamps.popleft()
        dac_values.popleft()
        tia_values.popleft()
        current_values.popleft()
        resistance_values.popleft()

    # Voltage
    dac_line.set_data(
        timestamps,
        dac_values
    )

    tia_line.set_data(
        timestamps,
        tia_values
    )

    # Current
    current_line.set_data(
        timestamps,
        current_values
    )

    # Resistance
    resistance_line.set_data(
        timestamps,
        resistance_values
    )

    # Sliding time axis
    left = max(0, current_time - WINDOW_SECONDS)
    right = max(WINDOW_SECONDS, current_time)

    ax1.set_xlim(left, right)

    return (
        dac_line,
        tia_line,
        current_line,
        resistance_line
    )

# =====================================================
# START LIVE PLOT
# =====================================================

ani = animation.FuncAnimation(
    fig,
    update_graph,
    interval=UPDATE_INTERVAL,
    blit=False,
    cache_frame_data=False
)

try:
    plt.show()

finally:
    ser.close()
    print("Serial connection closed.")
