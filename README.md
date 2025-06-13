# ESP32S3 Polar H10 ECG Visualizer

This is a **minimal ESP32S3 example** that connects to a **Polar H10 heart rate monitor**, receives **ECG data via BLE**, and visualizes it using the **NeoPixel** LED on the dev board. Each ECG sample animates the LED in a heartbeat-like manner. Built primarily for testing and exploration.

> ⚠️ This project is experimental and primarily AI-generated. Use at your own risk.

## Features

- Connects to Polar H10 over BLE (via Heart Rate Service UUID `0x180D`)
- Receives ECG data from PMD characteristic (`FB005C82-02E7-F387-1CAD-8ACD2D8DF0C8`)
- Visualizes real-time ECG signal using on-board NeoPixel
- Implements fallback BLE connect logic (no active scan callback)
- Timed LED display per ECG sample (~7ms interval)

## Hardware Requirements

- ESP32S3 Dev Board with built-in NeoPixel (tested with pin 48)
- Polar H10 chest strap
- Arduino IDE with:
  - [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
  - [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)

## Quick Start

1. Install required libraries via Arduino Library Manager
2. Flash the code to your ESP32S3
3. Power on the Polar H10
4. Open Serial Monitor (optional — see notes)
5. Press the **RESET** button if the device doesn't connect initially

> Note: After reset, allow ~15 seconds for the connection to succeed.

## Notes and Caveats

- **Do not use `Serial.print()`**: The latest NeoPixel library may conflict with `rmt tx` on ESP32S3.
- **Avoid using `String`** class: May also interfere with timing-sensitive operations.
- **LED update delay**: Default `delay(5)` is based on 7ms ECG sample rate minus ~1ms LED update time.
- **BLE connection issues**: Due to NimBLE’s broken active scan callbacks, the code uses blocking connect logic. Retry by resetting manually or implement retries yourself.

## Data Format (ECG)

Based on reverse engineering and [BleakHeart](https://github.com/kinnala/bleakheart):

- 130 Hz sampling
- 14-bit resolution, 3 bytes per ECG sample (24-bit signed int, little endian)
- See `polar_h10_ecg_specification.pdf` for details

## Credits

- [BleakHeart](https://github.com/kinnala/bleakheart) — Polar H10 reverse engineering
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — Lightweight BLE stack for ESP32
- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) — LED library

## Alternatives

- A simplified version for **cheap HR-only BLE bands (no ECG)** is also available.
- Try this if you don’t have a Polar H10 but still want to experiment with BLE + LED visualization.

