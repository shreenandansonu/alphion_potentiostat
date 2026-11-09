import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import time

# ================= SERIAL SETTINGS =================

PORT = "COM21"          # Change to your ESP32 COM port
BAUD_RATE = 230400

# ================= GRAPH SETTINGS ==================

WINDOW_SECONDS = 5     # Scrolling window
Y_MIN = -0.5
Y_MAX = 3.5

FILTER_ALPHA = 0.30    # Lower = smoother, higher = faster response
UPDATE_INTERVAL = 30   # Graph refresh interval in milliseconds

# ================= DATA STORAGE ====================

time_data = deque()
dac_data = deque()
adc_data = deque()

start_time = time.perf_counter()
filtered_adc = None

# ================= SERIAL CONNECTION ===============

try:
    ser = serial.Serial(
        PORT,
        BAUD_RATE,
        timeout=0.02
    )
    time.sleep(2)
    ser.reset_input_buffer()
    print(f"Connected to ESP32 on {PORT}")

except serial.SerialException as e:
    print("Serial connection failed:")
    print(e)
    raise SystemExit

# ================= GRAPH SETUP =====================

fig, ax = plt.subplots(figsize=(12, 6))

fig.patch.set_facecolor("white")
ax.set_facecolor("white")

# DAC expected voltage
dac_line, = ax.plot(
    [],
    [],
    color="blue",
    linewidth=2,
    label="DAC Expected Voltage"
)

# ADC measured voltage
adc_line, = ax.plot(
    [],
    [],
    color="green",
    linewidth=2,
    label="ADC Measured Voltage"
)

ax.set_title(
    "MCP4725 DAC vs ESP32 ADC Output",
    fontsize=15,
    color="black"
)

ax.set_xlabel("Time (seconds)", fontsize=12, color="black")
ax.set_ylabel("Voltage (V)", fontsize=12, color="black")

ax.set_ylim(Y_MIN, Y_MAX)
ax.set_xlim(0, WINDOW_SECONDS)

ax.grid(
    True,
    color="lightgray",
    linestyle="--",
    linewidth=0.7,
    alpha=0.8
)

ax.legend(
    loc="upper right",
    facecolor="white",
    edgecolor="gray",
    fontsize=10
)

ax.tick_params(axis="both", colors="black")

for spine in ax.spines.values():
    spine.set_color("black")

fig.tight_layout()

# ================= SERIAL DATA READER ==============

def read_serial_data():

    global filtered_adc

    while ser.in_waiting:

        try:
            raw = ser.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if not raw:
                continue

            # Skip CSV header
            if raw.startswith("TIME_MS"):
                continue

            values = raw.split(",")

            if len(values) < 4:
                continue

            # ESP32 CSV format:
            # TIME_MS,DAC_VALUE,EXPECTED_V,ADC_V

            expected_voltage = float(values[2])
            measured_voltage = float(values[3])

            # Smooth ADC signal
            if filtered_adc is None:
                filtered_adc = measured_voltage
            else:
                filtered_adc = (
                    FILTER_ALPHA * measured_voltage
                    + (1 - FILTER_ALPHA) * filtered_adc
                )

            current_time = time.perf_counter() - start_time

            time_data.append(current_time)
            dac_data.append(expected_voltage)
            adc_data.append(filtered_adc)

        except (ValueError, IndexError):
            continue


# ================= GRAPH UPDATE ====================

def update_graph(frame):

    read_serial_data()

    if not time_data:
        return dac_line, adc_line

    current_time = time_data[-1]

    # Remove old samples outside the window
    while time_data and time_data[0] < current_time - WINDOW_SECONDS:
        time_data.popleft()
        dac_data.popleft()
        adc_data.popleft()

    # Update plotted data
    dac_line.set_data(time_data, dac_data)
    adc_line.set_data(time_data, adc_data)

    # Sliding time window
    ax.set_xlim(
        max(0, current_time - WINDOW_SECONDS),
        max(WINDOW_SECONDS, current_time)
    )

    return dac_line, adc_line


# ================= START ANIMATION =================

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