
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import time

# ==========================================
# SERIAL CONFIGURATION
# ==========================================

PORT = "COM21"          # Change to your ESP32 COM port
BAUD_RATE = 230400

# ==========================================
# GRAPH CONFIGURATION
# ==========================================

WINDOW_SECONDS = 10
UPDATE_INTERVAL = 50

Y_MIN = -0.1
Y_MAX = 2.1

MAX_POINTS = 5000

# ==========================================
# DATA STORAGE
# ==========================================

timestamps = deque(maxlen=MAX_POINTS)
dac_values = deque(maxlen=MAX_POINTS)
adc_values = deque(maxlen=MAX_POINTS)
error_values = deque(maxlen=MAX_POINTS)

# ==========================================
# SERIAL CONNECTION
# ==========================================

print("Connecting to ESP32...")

ser = serial.Serial(
    PORT,
    BAUD_RATE,
    timeout=0.02
)

time.sleep(2)
ser.reset_input_buffer()

print("Connected successfully!")

# ==========================================
# GRAPH INITIALIZATION
# ==========================================

fig, ax = plt.subplots(figsize=(13, 7))

fig.patch.set_facecolor("white")
ax.set_facecolor("white")

# DAC trace
dac_line, = ax.plot(
    [],
    [],
    color="royalblue",
    linewidth=2,
    label="MCP4725 - Expected Voltage"
)

# ADC trace
adc_line, = ax.plot(
    [],
    [],
    color="seagreen",
    linewidth=2,
    label="MCP3421 - Measured Voltage"
)

# Graph formatting
ax.set_title(
    "MCP4725 - MCP3421 Synchronized Test",
    fontsize=16,
    fontweight="bold",
    color="black"
)

ax.set_xlabel("Time (seconds)", fontsize=12, color="black")
ax.set_ylabel("Voltage (V)", fontsize=12, color="black")

ax.set_ylim(Y_MIN, Y_MAX)
ax.set_xlim(0, WINDOW_SECONDS)

ax.grid(
    True,
    linestyle="--",
    linewidth=0.7,
    color="lightgray",
    alpha=0.8
)

ax.tick_params(axis="both", colors="black")

for spine in ax.spines.values():
    spine.set_color("black")

ax.legend(
    loc="upper right",
    facecolor="white",
    edgecolor="gray",
    fontsize=10
)

fig.tight_layout()

# ==========================================
# SERIAL DATA READER
# ==========================================

def read_serial_data():

    while ser.in_waiting:

        raw = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not raw:
            continue

        # Ignore CSV header
        if raw.startswith("TIME_MS"):
            continue

        # Display ESP32 errors
        if raw.startswith("ERROR"):
            print(raw)
            continue

        try:
            values = raw.split(",")

            if len(values) != 5:
                continue

            mcu_time = int(values[0])
            dac_code = int(values[1])
            expected_voltage = float(values[2])
            measured_voltage = float(values[3])
            error_mV = float(values[4])

            # ESP32 timestamp in seconds
            timestamp = mcu_time / 1000.0

            timestamps.append(timestamp)
            dac_values.append(expected_voltage)
            adc_values.append(measured_voltage)
            error_values.append(error_mV)

        except ValueError:
            continue

# ==========================================
# GRAPH UPDATE
# ==========================================

def update_graph(frame):

    read_serial_data()

    if not timestamps:
        return dac_line, adc_line

    current_time = timestamps[-1]

    # Remove samples outside scrolling window
    while (
        timestamps
        and timestamps[0] < current_time - WINDOW_SECONDS
    ):
        timestamps.popleft()
        dac_values.popleft()
        adc_values.popleft()
        error_values.popleft()

    # Update traces
    dac_line.set_data(
        timestamps,
        dac_values
    )

    adc_line.set_data(
        timestamps,
        adc_values
    )

    # Scrolling time axis
    left = max(0, current_time - WINDOW_SECONDS)
    right = max(WINDOW_SECONDS, current_time)

    ax.set_xlim(left, right)

    return dac_line, adc_line

# ==========================================
# START LIVE PLOT
# ==========================================

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
