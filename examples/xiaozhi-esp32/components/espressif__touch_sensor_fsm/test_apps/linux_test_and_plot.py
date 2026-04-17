#!/usr/bin/env python3
# filepath: /home/libo/touch_sensor_libs/components/touch_sensor_fsm/test_apps/linux_test_and_plot.py
import matplotlib.pyplot as plt
import sys
from collections import defaultdict
import os
import subprocess
from datetime import datetime

def parse_log_file(log_file):
    data = defaultdict(lambda: {'timestamps': [], 'raw_values': [], 'smooth_values': [], 'benchmark_values': [], 'triggers': [], 'buttons': []})

    with open(log_file, 'r') as file:
        for line in file:
            if line.startswith('vl'):
                parts = line.strip().split(',')
                channel = int(parts[2])
                timestamp = len(data[channel]['timestamps']) * 20  # Fixed time step of 20 ms
                data[channel]['timestamps'].append(timestamp)
                data[channel]['raw_values'].append(int(parts[3]))
                data[channel]['smooth_values'].append(int(parts[4]))
                data[channel]['benchmark_values'].append(int(parts[5]))
            elif line.startswith('tg'):
                parts = line.strip().split(',')
                channel = int(parts[2])
                timestamp = len(data[channel]['timestamps']) * 20  # Fixed time step of 20 ms
                if_active = int(parts[5])
                data[channel]['triggers'].append((timestamp, if_active))
            elif line.startswith('bt'):
                parts = line.strip().split(',')
                channel = int(parts[2])
                timestamp = len(data[channel]['timestamps']) * 20  # Fixed time step of 20 ms
                if_active = int(parts[5])
                data[channel]['buttons'].append((timestamp, if_active))

    return data

def plot_values(data, log_file):
    base_name = os.path.splitext(os.path.basename(log_file))[0]
    output_folder = os.path.dirname(log_file)
    for channel, values in data.items():
        plt.figure(figsize=(10, 6))
        plt.plot(values['timestamps'], values['raw_values'], label='Raw', marker='o')
        plt.plot(values['timestamps'], values['smooth_values'], label='Smooth', marker='o')
        plt.plot(values['timestamps'], values['benchmark_values'], label='Benchmark', marker='o')
        for trigger in values['triggers']:
            color = 'r' if trigger[1] > 0 else 'g'
            plt.axvline(x=trigger[0], color=color, ymin=0.2, ymax=0.8, linestyle='dotted', label='Trigger' if trigger == values['triggers'][0] else '')
        for button in values['buttons']:
            color = 'b' if button[1] > 0 else 'y'
            plt.axvline(x=button[0], color=color, ymin=0.25, ymax=0.75, linestyle='--', label='Button' if button == values['buttons'][0] else '')
        plt.xlabel('Time (ms)')
        plt.ylabel('Values')
        plt.title(f'Touch Sensor Values for Channel {channel}')
        plt.legend()
        plt.grid(True)
        plt.savefig(os.path.join(output_folder, f"{base_name}_channel_{channel}.png"), format='png')
        plt.show()
        plt.close()

if __name__ == '__main__':
    output_folder = 'output'
    os.makedirs(output_folder, exist_ok=True)

    if len(sys.argv) != 2:
        log_file = os.path.join(output_folder, f"log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt")
        subprocess.run(['/bin/bash', 'linux_test.sh', log_file], check=True)
    else:
        log_file = sys.argv[1]

    # Run the linux_test.sh script to generate the log file
    data = parse_log_file(log_file)
    plot_values(data, log_file)
