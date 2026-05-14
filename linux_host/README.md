# Linux Host Tools for ADC Data Acquisition

Two complementary tools for reading ADC data from the STM32F429-Discovery via libusb.

## Building

```bash
make
```

This creates two executables:
- `libusb_reader` - Simple binary data reader
- `data_logger` - Advanced logger with CSV export and statistics

### Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install libusb-1.0-0-dev

# RHEL/CentOS
sudo yum install libusb1-devel

# macOS
brew install libusb
```

## Tool 1: libusb_reader

Simple tool for capturing raw binary ADC data to a file.

### Usage
```bash
./libusb_reader <output_file>
```

### Example
```bash
./libusb_reader measurements.raw
# Device found!
# Reading ADC data from device...
# Press Ctrl+C to stop
# 
# Received: 100000 samples (0.48 MB)
# Transfer complete!
# Total samples received: 5234567
# Data file size: 25.09 MB
```

### Output Format
Raw binary file containing consecutive `adc_sample` structures:
```c
struct adc_sample {
    uint64_t timestamp;   // 8 bytes
    uint16_t channel_0;   // 2 bytes
    uint16_t channel_1;   // 2 bytes
    uint16_t channel_2;   // 2 bytes
};  // Total: 12 bytes per sample
```

## Tool 2: data_logger

Advanced data logging tool with real-time statistics and CSV export.

### Usage
```bash
./data_logger <num_samples> <csv_file>
```

### Example
```bash
./data_logger 1000000 adc_samples.csv
# Collecting 1000000 samples...
# Collected: 100000 / 1000000 samples
# Collected: 200000 / 1000000 samples
# ...
# 
# === Data Collection Complete ===
# Total samples: 1000000
# 
# === Channel Statistics ===
# Channel 0: Min=156, Max=4032, Mean=2048.45, StdDev=1152.34
# Channel 1: Min=203, Max=4089, Mean=2087.23, StdDev=1190.12
# Channel 2: Min=98, Max=4015, Mean=1956.78, StdDev=1100.56
# 
# CSV file written: adc_samples.csv
```

### Output Format
CSV file with headers:
```
Timestamp_us,Channel_0,Channel_1,Channel_2
0,2048,2050,2045
1,2051,2052,2048
2,2049,2051,2047
```

### Statistics
The data_logger calculates and displays for each channel:
- **Min**: Minimum ADC value
- **Max**: Maximum ADC value  
- **Mean**: Average ADC value
- **StdDev**: Standard deviation (signal variation)

## Common Tasks

### Capture 10 Million Samples
```bash
./data_logger 10000000 large_dataset.csv
# Takes ~10 seconds at 1 MSPS
```

### Analyze Signal in Python
```python
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

df = pd.read_csv('adc_samples.csv')

# Plot all channels
plt.figure(figsize=(12, 6))
plt.plot(df['Timestamp_us'][:10000], df['Channel_0'][:10000], label='Ch0')
plt.plot(df['Timestamp_us'][:10000], df['Channel_1'][:10000], label='Ch1')
plt.plot(df['Timestamp_us'][:10000], df['Channel_2'][:10000], label='Ch2')
plt.xlabel('Time (µs)')
plt.ylabel('ADC Value')
plt.legend()
plt.title('ADC Samples')
plt.savefig('adc_plot.png')

# Frequency analysis
from scipy import signal
f0, pxx = signal.periodogram(df['Channel_0'], fs=1e6)
plt.semilogy(f0, pxx)
plt.xlabel('Frequency (Hz)')
plt.ylabel('Power')
plt.title('FFT of Channel 0')
plt.savefig('fft_plot.png')
```

### Convert to MATLAB Format
```bash
python3 << 'EOF'
import pandas as pd
import scipy.io as sio

df = pd.read_csv('adc_samples.csv')
sio.savemat('adc_data.mat', {
    'timestamp': df['Timestamp_us'].values,
    'ch0': df['Channel_0'].values,
    'ch1': df['Channel_1'].values,
    'ch2': df['Channel_2'].values,
})
EOF
```

## USB Permissions

If you get "Device not found" or "Permission denied":

```bash
# Check device
lsusb | grep 2fe3
# Bus 001 Device 007: ID 2fe3:0100

# Add udev rule (one-time setup)
sudo tee /etc/udev/rules.d/90-stm32f429.rules > /dev/null <<EOF
SUBSYSTEM=="usb", ATTRS{idVendor}=="2fe3", ATTRS{idProduct}=="0100", MODE="0666"
EOF

# Reload udev rules
sudo udevadm control --reload
sudo udevadm trigger

# Reconnect device or restart computer
```

## Performance Optimization

### For Maximum Speed
```bash
# Ensure USB device is on fast hub (not behind a bridge)
lsusb -t

# Monitor transfer rate
time ./libusb_reader test.raw &
watch -n1 'ls -lh test.raw'  # In another terminal
```

### For Minimal CPU Usage
The tools use blocking I/O to minimize CPU. On typical Linux systems:
- 1 MSPS sampling: ~2-5% CPU
- No additional optimization needed

## Troubleshooting

### "Device not found"
```bash
# Check if device is enumerated
lsusb -v | grep -A5 2fe3

# Check kernel messages
dmesg | tail -20

# Verify firmware is running
west term  # View serial output
```

### "Permission denied"
```bash
# Run with sudo (temporary)
sudo ./libusb_reader test.raw

# Or setup udev rule (permanent, see above)
```

### "Transfer timeout"
```bash
# Check USB connection quality
# Try different USB port
# Check cable integrity

# Verify device responsiveness
echo "Testing device..." && timeout 2 ./libusb_reader /dev/null
```

### Incomplete Data
If the file is smaller than expected:
- Data transfer was interrupted (check USB connection)
- Specified sample count was never reached
- Use larger buffer: `USB_BULK_MAX_PACKET_SIZE 512`

## API Reference

Both tools use the same communication protocol:

```c
#define VENDOR_ID  0x2FE3
#define PRODUCT_ID 0x0100
#define USB_ENDPOINT_IN  0x81
#define USB_TIMEOUT 5000

struct adc_sample {
    uint64_t timestamp;
    uint16_t channel_0;
    uint16_t channel_1;
    uint16_t channel_2;
} __attribute__((packed));
```

To integrate into your own application, use libusb directly:

```c
#include <libusb-1.0/libusb.h>

libusb_init(&ctx);
dev = libusb_open_device_with_vid_pid(ctx, 0x2FE3, 0x0100);
libusb_claim_interface(dev, 0);
libusb_bulk_transfer(dev, 0x81, buf, size, &actual, 5000);
```

## License

MIT License
