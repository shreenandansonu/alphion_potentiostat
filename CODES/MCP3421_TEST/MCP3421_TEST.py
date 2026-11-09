
import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import time

# ============== CONFIGURATION ==============

COM_PORT = 'COM21'       # Change to your ESP32 COM port
BAUD_RATE = 115200

WINDOW_SECONDS = 5      # Sliding window duration

ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=0.02)

# ============== DATA STORAGE ==============

timestamps = deque()
expected_voltage = deque()
measured_voltage = deque()

start_time = time.perf_counter()

# ============== PLOT SETUP ==============

fig, ax = plt.subplots(figsize=(11, 6))

line1, = ax.plot(
    [], [],
    color='blue',
    linewidth=2,
    label='Expected DAC Voltage'
)

line2, = ax.plot(
    [], [],
    color='green',
    linewidth=2,
    label='MCP3421 ADC Voltage'
)

ax.set_title('Live ESP32 DAC - MCP3421 ADC')
ax.set_xlabel('Time (seconds)')
ax.set_ylabel('Voltage (V)')

ax.set_ylim(-0.1, 2.3)
ax.set_xlim(0, WINDOW_SECONDS)

ax.grid(True, alpha=0.3)
ax.legend(loc='upper right')

# Live voltage display
status = ax.text(
    0.02, 0.95, '',
    transform=ax.transAxes,
    verticalalignment='top',
    fontsize=10
)

plt.tight_layout()

# ============== DATA READER ==============

def update(frame):

    global start_time

    latest_dac = None
    latest_adc = None

    # Read all currently available serial lines
    while ser.in_waiting:

        data = ser.readline().decode(
            'utf-8', errors='ignore'
        ).strip()

        values = data.split(',')

        if len(values) != 4:
            continue

        try:
            esp_time, dac, expected, measured = map(
                float, values
            )
        except ValueError:
            continue

        current_time = time.perf_counter() - start_time

        timestamps.append(current_time)
        expected_voltage.append(expected)
        measured_voltage.append(measured)

        latest_dac = dac
        latest_adc = measured

    now = time.perf_counter() - start_time

    # Remove data outside sliding window
    while timestamps and timestamps[0] < now - WINDOW_SECONDS:
        timestamps.popleft()
        expected_voltage.popleft()
        measured_voltage.popleft()

    # Update curves
    line1.set_data(timestamps, expected_voltage)
    line2.set_data(timestamps, measured_voltage)

    # Slide the X-axis
    ax.set_xlim(
        max(0, now - WINDOW_SECONDS),
        max(WINDOW_SECONDS, now)
    )

    # Live readings
    if latest_adc is not None:

        status.set_text(
            f"DAC: {latest_dac:.0f} | "
            f"ADC: {latest_adc:.4f} V"
        )

    return line1, line2, status


# ============== START LIVE PLOT ==============

ani = animation.FuncAnimation(
    fig,
    update,
    interval=30,
    blit=False,
    cache_frame_data=False
)

print("Live acquisition started...")
print("Close graph window to stop.")

try:
    plt.show()
finally:
    ser.close()
    print("Serial connection closed.")
